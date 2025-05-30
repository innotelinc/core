/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */
#include <sal/macros.h>
#include <sal/log.hxx>
#include <rtl/math.hxx>
#include <formula/FormulaCompiler.hxx>
#include <formula/errorcodes.hxx>
#include <formula/token.hxx>
#include <formula/tokenarray.hxx>
#include <o3tl/string_view.hxx>
#include <core_resource.hxx>
#include <core_resource.hrc>

#include <svl/zforlist.hxx>
#include <unotools/charclass.hxx>
#include <vcl/svapp.hxx>
#include <vcl/settings.hxx>
#include <comphelper/lok.hxx>
#include <comphelper/processfactory.hxx>
#include <com/sun/star/sheet/FormulaOpCodeMapEntry.hpp>
#include <com/sun/star/sheet/FormulaMapGroup.hpp>
#include <com/sun/star/sheet/FormulaMapGroupSpecialOffset.hpp>
#include <algorithm>
#include <mutex>

namespace formula
{
    using namespace ::com::sun::star;

namespace {

class FormulaCompilerRecursionGuard
{
    private:
        short& rRecursion;
    public:
        explicit FormulaCompilerRecursionGuard( short& rRec )
            : rRecursion( rRec ) { ++rRecursion; }
        ~FormulaCompilerRecursionGuard() { --rRecursion; }
};

SvNumFormatType lcl_GetRetFormat( OpCode eOpCode )
{
    switch (eOpCode)
    {
        case ocEqual:
        case ocNotEqual:
        case ocLess:
        case ocGreater:
        case ocLessEqual:
        case ocGreaterEqual:
        case ocAnd:
        case ocOr:
        case ocXor:
        case ocNot:
        case ocTrue:
        case ocFalse:
        case ocIsEmpty:
        case ocIsString:
        case ocIsNonString:
        case ocIsLogical:
        case ocIsRef:
        case ocIsValue:
        case ocIsFormula:
        case ocIsNA:
        case ocIsErr:
        case ocIsError:
        case ocIsEven:
        case ocIsOdd:
        case ocExact:
            return SvNumFormatType::LOGICAL;
        case ocGetActDate:
        case ocGetDate:
        case ocEasterSunday :
            return SvNumFormatType::DATE;
        case ocGetActTime:
            return SvNumFormatType::DATETIME;
        case ocGetTime:
            return SvNumFormatType::TIME;
        case ocNPV:
        case ocPV:
        case ocSYD:
        case ocDDB:
        case ocDB:
        case ocVBD:
        case ocSLN:
        case ocPMT:
        case ocFV:
        case ocIpmt:
        case ocPpmt:
        case ocCumIpmt:
        case ocCumPrinc:
            return SvNumFormatType::CURRENCY;
        case ocRate:
        case ocIRR:
        case ocMIRR:
        case ocRRI:
        case ocEffect:
        case ocNominal:
        case ocPercentSign:
            return SvNumFormatType::PERCENT;
        default:
            return SvNumFormatType::NUMBER;
    }
}

void lclPushOpCodeMapEntry( ::std::vector< sheet::FormulaOpCodeMapEntry >& rVec,
        const OUString* pTable, sal_uInt16 nOpCode )
{
    sheet::FormulaOpCodeMapEntry aEntry;
    aEntry.Token.OpCode = nOpCode;
    aEntry.Name = pTable[nOpCode];
    rVec.push_back( aEntry);
}

void lclPushOpCodeMapEntries( ::std::vector< sheet::FormulaOpCodeMapEntry >& rVec,
        const OUString* pTable, sal_uInt16 nOpCodeBeg, sal_uInt16 nOpCodeEnd )
{
    for (sal_uInt16 nOpCode = nOpCodeBeg; nOpCode < nOpCodeEnd; ++nOpCode)
        lclPushOpCodeMapEntry( rVec, pTable, nOpCode );
}

void lclPushOpCodeMapEntries( ::std::vector< sheet::FormulaOpCodeMapEntry >& rVec,
        const OUString* pTable, const sal_uInt16* pnOpCodes, size_t nCount )
{
    for (const sal_uInt16* pnEnd = pnOpCodes + nCount; pnOpCodes < pnEnd; ++pnOpCodes)
        lclPushOpCodeMapEntry( rVec, pTable, *pnOpCodes );
}

CharClass* createCharClassIfNonEnglishUI()
{
    const LanguageTag& rLanguageTag( Application::GetSettings().GetUILanguageTag());
    if (rLanguageTag.getLanguage() == "en")
        return nullptr;
    return new CharClass( ::comphelper::getProcessComponentContext(), rLanguageTag);
}

class OpCodeList
{
public:

    OpCodeList(const std::pair<const char*, int>* pSymbols, const FormulaCompiler::NonConstOpCodeMapPtr&,
            FormulaCompiler::SeparatorType = FormulaCompiler::SeparatorType::SEMICOLON_BASE );
    OpCodeList(const std::pair<TranslateId, int>* pSymbols, const FormulaCompiler::NonConstOpCodeMapPtr&,
            FormulaCompiler::SeparatorType = FormulaCompiler::SeparatorType::SEMICOLON_BASE );

private:
    bool getOpCodeString( OUString& rStr, sal_uInt16 nOp );
    void putDefaultOpCode( const FormulaCompiler::NonConstOpCodeMapPtr& xMap, sal_uInt16 nOp, const CharClass* pCharClass );

private:
    FormulaCompiler::SeparatorType meSepType;
    const std::pair<const char*, int>* mpSymbols1;
    const std::pair<TranslateId, int>* mpSymbols2;
};

OpCodeList::OpCodeList(const std::pair<const char*, int>* pSymbols, const FormulaCompiler::NonConstOpCodeMapPtr& xMap,
        FormulaCompiler::SeparatorType eSepType)
    : meSepType(eSepType)
    , mpSymbols1(pSymbols)
    , mpSymbols2(nullptr)
{
    std::unique_ptr<CharClass> xCharClass( xMap->isEnglish() ? nullptr : createCharClassIfNonEnglishUI());
    const CharClass* pCharClass = xCharClass.get();
    if (meSepType == FormulaCompiler::SeparatorType::RESOURCE_BASE)
    {
        for (sal_uInt16 i = 0; i <= SC_OPCODE_LAST_OPCODE_ID; ++i)
        {
            putDefaultOpCode( xMap, i, pCharClass);
        }
    }
    else
    {
        for (sal_uInt16 i = 0; i <= SC_OPCODE_LAST_OPCODE_ID; ++i)
        {
            OUString aOpStr;
            if ( getOpCodeString( aOpStr, i) )
                xMap->putOpCode( aOpStr, OpCode(i), pCharClass);
            else
                putDefaultOpCode( xMap, i, pCharClass);
        }
    }
}

OpCodeList::OpCodeList(const std::pair<TranslateId, int>* pSymbols, const FormulaCompiler::NonConstOpCodeMapPtr& xMap,
        FormulaCompiler::SeparatorType eSepType)
    : meSepType(eSepType)
    , mpSymbols1(nullptr)
    , mpSymbols2(pSymbols)
{
    std::unique_ptr<CharClass> xCharClass( xMap->isEnglish() ? nullptr : createCharClassIfNonEnglishUI());
    const CharClass* pCharClass = xCharClass.get();
    if (meSepType == FormulaCompiler::SeparatorType::RESOURCE_BASE)
    {
        for (sal_uInt16 i = 0; i <= SC_OPCODE_LAST_OPCODE_ID; ++i)
        {
            putDefaultOpCode( xMap, i, pCharClass);
        }
    }
    else
    {
        for (sal_uInt16 i = 0; i <= SC_OPCODE_LAST_OPCODE_ID; ++i)
        {
            OUString aOpStr;
            if ( getOpCodeString( aOpStr, i) )
                xMap->putOpCode( aOpStr, OpCode(i), pCharClass);
            else
                putDefaultOpCode( xMap, i, pCharClass);
        }
    }
}

bool OpCodeList::getOpCodeString( OUString& rStr, sal_uInt16 nOp )
{
    switch (nOp)
    {
        case SC_OPCODE_SEP:
        {
            if (meSepType == FormulaCompiler::SeparatorType::SEMICOLON_BASE)
            {
                rStr = ";";
                return true;
            }
        }
        break;
        case SC_OPCODE_ARRAY_COL_SEP:
        {
            if (meSepType == FormulaCompiler::SeparatorType::SEMICOLON_BASE)
            {
                rStr = ";";
                return true;
            }
        }
        break;
        case SC_OPCODE_ARRAY_ROW_SEP:
        {
            if (meSepType == FormulaCompiler::SeparatorType::SEMICOLON_BASE)
            {
                rStr = "|";
                return true;
            }
        }
        break;
    }

    return false;
}

void OpCodeList::putDefaultOpCode( const FormulaCompiler::NonConstOpCodeMapPtr& xMap, sal_uInt16 nOp,
        const CharClass* pCharClass )
{
    OUString sKey;
    if (mpSymbols1)
    {
        const char* pKey = nullptr;
        for (const std::pair<const char*, int>* pSymbol = mpSymbols1; pSymbol->first; ++pSymbol)
        {
            if (nOp == pSymbol->second)
            {
                pKey = pSymbol->first;
                break;
            }
        }
        if (!pKey)
            return;
        sKey = OUString::createFromAscii(pKey);
    }
    else if (mpSymbols2)
    {
        TranslateId pKey;
        for (const std::pair<TranslateId, int>* pSymbol = mpSymbols2; pSymbol->first; ++pSymbol)
        {
            if (nOp == pSymbol->second)
            {
                pKey = pSymbol->first;
                break;
            }
        }
        if (!pKey)
            return;
        sKey = ForResId(pKey);
    }
    xMap->putOpCode(sKey, OpCode(nOp), pCharClass);
}

// static
const sal_Unicode* lcl_UnicodeStrChr( const sal_Unicode* pStr, sal_Unicode c )
{
    if ( !pStr )
        return nullptr;
    while ( *pStr )
    {
        if ( *pStr == c )
            return pStr;
        pStr++;
    }
    return nullptr;
}

struct OpCodeMapData
{
    FormulaCompiler::NonConstOpCodeMapPtr mxSymbolMap;
    std::mutex maMtx;
};


bool isPotentialRangeLeftOp( OpCode eOp )
{
    switch (eOp)
    {
        case ocClose:
            return true;
        default:
            return false;
    }
}

bool isRangeResultFunction( OpCode eOp )
{
    switch (eOp)
    {
        case ocIndirect:
        case ocOffset:
            return true;
        default:
            return false;
    }
}

bool isRangeResultOpCode( OpCode eOp )
{
    switch (eOp)
    {
        case ocRange:
        case ocUnion:
        case ocIntersect:
        case ocIndirect:
        case ocOffset:
            return true;
        default:
            return false;
    }
}

/**
    @param  pToken
            MUST be a valid token, caller has to ensure.

    @param  bRight
            If bRPN==false, bRight==false means opcodes for left side are
            checked, bRight==true means opcodes for right side. If bRPN==true
            it doesn't matter except for the ocSep converted to ocUnion case.
 */
bool isPotentialRangeType( FormulaToken const * pToken, bool bRPN, bool bRight )
{
    switch (pToken->GetType())
    {
        case svByte:                // could be range result, but only a few
            if (bRPN)
                return isRangeResultOpCode( pToken->GetOpCode());
            else if (bRight)
                return isRangeResultFunction( pToken->GetOpCode());
            else
                return isPotentialRangeLeftOp( pToken->GetOpCode());
        case svSingleRef:
        case svDoubleRef:
        case svIndex:               // could be range
        //case svRefList:           // um..what?
        case svExternalSingleRef:
        case svExternalDoubleRef:
        case svExternalName:        // could be range
            return true;
        case svSep:
            // A special case if a previous ocSep was converted to ocUnion it
            // stays svSep instead of svByte.
            return bRPN && !bRight && pToken->GetOpCode() == ocUnion;
        default:
            // Separators are not part of RPN and right opcodes need to be
            // other StackVar types or functions and thus svByte.
            return !bRPN && !bRight && isPotentialRangeLeftOp( pToken->GetOpCode());
    }
}

bool isIntersectable( FormulaToken** pCode1, FormulaToken** pCode2 )
{
    FormulaToken* pToken1 = *pCode1;
    FormulaToken* pToken2 = *pCode2;
    if (pToken1 && pToken2)
        return isPotentialRangeType( pToken1, true, false) && isPotentialRangeType( pToken2, true, true);
    return false;
}

bool isAdjacentRpnEnd( sal_uInt16 nPC,
        FormulaToken const * const * const pCode,
        FormulaToken const * const * const pCode1,
        FormulaToken const * const * const pCode2 )
{
    return nPC >= 2 && pCode1 && pCode2 &&
            (pCode2 - pCode1 == 1) && (pCode - pCode2 == 1) &&
            (*pCode1 != nullptr) && (*pCode2 != nullptr);
}

bool isAdjacentOrGapRpnEnd( sal_uInt16 nPC,
        FormulaToken const * const * const pCode,
        FormulaToken const * const * const pCode1,
        FormulaToken const * const * const pCode2 )
{
    return nPC >= 2 && pCode1 && pCode2 &&
            (pCode2 > pCode1) && (pCode - pCode2 == 1) &&
            (*pCode1 != nullptr) && (*pCode2 != nullptr);
}


} // namespace


void FormulaCompiler::OpCodeMap::putExternal( const OUString & rSymbol, const OUString & rAddIn )
{
    // Different symbols may map to the same AddIn, but the same AddIn may not
    // map to different symbols, the first pair wins. Same symbol of course may
    // not map to different AddIns, again the first pair wins and also the
    // AddIn->symbol mapping is not inserted in other cases.
    bool bOk = maExternalHashMap.emplace(rSymbol, rAddIn).second;
    SAL_WARN_IF( !bOk, "formula.core", "OpCodeMap::putExternal: symbol not inserted, " << rSymbol << " -> " << rAddIn);
    if (bOk)
    {
        bOk = maReverseExternalHashMap.emplace(rAddIn, rSymbol).second;
        // Failed insertion of the AddIn is ok for different symbols mapping to
        // the same AddIn. Make this INFO only.
        SAL_INFO_IF( !bOk, "formula.core", "OpCodeMap::putExternal: AddIn not inserted, " << rAddIn << " -> " << rSymbol);
    }
}

void FormulaCompiler::OpCodeMap::putExternalSoftly( const OUString & rSymbol, const OUString & rAddIn )
{
    // Same as putExternal() but no warning, instead info whether inserted or not.
    bool bOk = maExternalHashMap.emplace(rSymbol, rAddIn).second;
    SAL_INFO( "formula.core", "OpCodeMap::putExternalSoftly: symbol " << (bOk ? "" : "not ") << "inserted, " << rSymbol << " -> " << rAddIn);
    if (bOk)
    {
        bOk = maReverseExternalHashMap.emplace(rAddIn, rSymbol).second;
        SAL_INFO_IF( !bOk, "formula.core", "OpCodeMap::putExternalSoftly: AddIn not inserted, " << rAddIn << " -> " << rSymbol);
    }
}

uno::Sequence< sheet::FormulaToken > FormulaCompiler::OpCodeMap::createSequenceOfFormulaTokens(
        const FormulaCompiler& rCompiler, const uno::Sequence< OUString >& rNames ) const
{
    const sal_Int32 nLen = rNames.getLength();
    uno::Sequence< sheet::FormulaToken > aTokens( nLen);
    sheet::FormulaToken* pToken = aTokens.getArray();
    OUString const * pName = rNames.getConstArray();
    OUString const * const pStop = pName + nLen;
    for ( ; pName < pStop; ++pName, ++pToken)
    {
        OpCodeHashMap::const_iterator iLook( maHashMap.find( *pName));
        if (iLook != maHashMap.end())
            pToken->OpCode = (*iLook).second;
        else
        {
            OUString aIntName;
            if (hasExternals())
            {
                ExternalHashMap::const_iterator iExt( maExternalHashMap.find( *pName));
                if (iExt != maExternalHashMap.end())
                    aIntName = (*iExt).second;
                // Check for existence not needed here, only name-mapping is of
                // interest.
            }
            if (aIntName.isEmpty())
                aIntName = rCompiler.FindAddInFunction(*pName, !isEnglish());    // bLocalFirst=false for english
            if (aIntName.isEmpty())
                pToken->OpCode = getOpCodeUnknown();
            else
            {
                pToken->OpCode = ocExternal;
                pToken->Data <<= aIntName;
            }
        }
    }
    return aTokens;
}

uno::Sequence< sheet::FormulaOpCodeMapEntry > FormulaCompiler::OpCodeMap::createSequenceOfAvailableMappings(
        const FormulaCompiler& rCompiler, const sal_Int32 nGroups ) const
{
    using namespace sheet;

    // Unfortunately uno::Sequence can't grow without cumbersome reallocs. As
    // we don't know in advance how many elements it will have we use a
    // temporary vector to add elements and then copy to Sequence :-(
    ::std::vector< FormulaOpCodeMapEntry > aVec;

    if (nGroups == FormulaMapGroup::SPECIAL)
    {
        // Use specific order, keep in sync with
        // offapi/com/sun/star/sheet/FormulaMapGroupSpecialOffset.idl
        static const struct
        {
            sal_Int32 nOff;
            OpCode    eOp;
        } aMap[] = {
            { FormulaMapGroupSpecialOffset::PUSH              , ocPush }           ,
            { FormulaMapGroupSpecialOffset::CALL              , ocCall }           ,
            { FormulaMapGroupSpecialOffset::STOP              , ocStop }           ,
            { FormulaMapGroupSpecialOffset::EXTERNAL          , ocExternal }       ,
            { FormulaMapGroupSpecialOffset::NAME              , ocName }           ,
            { FormulaMapGroupSpecialOffset::NO_NAME           , ocNoName }         ,
            { FormulaMapGroupSpecialOffset::MISSING           , ocMissing }        ,
            { FormulaMapGroupSpecialOffset::BAD               , ocBad }            ,
            { FormulaMapGroupSpecialOffset::SPACES            , ocSpaces }         ,
            { FormulaMapGroupSpecialOffset::MAT_REF           , ocMatRef }         ,
            { FormulaMapGroupSpecialOffset::DB_AREA           , ocDBArea }         ,
            { FormulaMapGroupSpecialOffset::MACRO             , ocMacro }          ,
            { FormulaMapGroupSpecialOffset::COL_ROW_NAME      , ocColRowName }     ,
            { FormulaMapGroupSpecialOffset::WHITESPACE        , ocWhitespace }     ,
            { FormulaMapGroupSpecialOffset::TABLE_REF         , ocTableRef }
        };
        const size_t nCount = SAL_N_ELEMENTS(aMap);
        // Preallocate vector elements.
        FormulaOpCodeMapEntry aEntry;
        aEntry.Token.OpCode = getOpCodeUnknown();
        aVec.resize(nCount, aEntry);

        for (auto& i : aMap)
        {
            size_t nIndex = static_cast< size_t >( i.nOff );
            if (aVec.size() <= nIndex)
            {
                // The offsets really should be aligned with the size, so if
                // the vector was preallocated above this code to resize it is
                // just a measure in case the table isn't in sync with the API,
                // usually it isn't executed.
                aEntry.Token.OpCode = getOpCodeUnknown();
                aVec.resize( nIndex + 1, aEntry );
            }
            aEntry.Token.OpCode = i.eOp;
            aVec[nIndex] = aEntry;
        }
    }
    else
    {
        /* FIXME: Once we support error constants in formulas we'll need a map
         * group for that, e.g. FormulaMapGroup::ERROR_CONSTANTS, and fill
         * SC_OPCODE_START_ERRORS to SC_OPCODE_STOP_ERRORS. */

        // Anything else but SPECIAL.
        if ((nGroups & FormulaMapGroup::SEPARATORS) != 0)
        {
            static const sal_uInt16 aOpCodes[] = {
                SC_OPCODE_OPEN,
                SC_OPCODE_CLOSE,
                SC_OPCODE_SEP,
            };
            lclPushOpCodeMapEntries( aVec, mpTable.get(), aOpCodes, SAL_N_ELEMENTS(aOpCodes) );
        }
        if ((nGroups & FormulaMapGroup::ARRAY_SEPARATORS) != 0)
        {
            static const sal_uInt16 aOpCodes[] = {
                SC_OPCODE_ARRAY_OPEN,
                SC_OPCODE_ARRAY_CLOSE,
                SC_OPCODE_ARRAY_ROW_SEP,
                SC_OPCODE_ARRAY_COL_SEP
            };
            lclPushOpCodeMapEntries( aVec, mpTable.get(), aOpCodes, SAL_N_ELEMENTS(aOpCodes) );
        }
        if ((nGroups & FormulaMapGroup::UNARY_OPERATORS) != 0)
        {
            // Due to the nature of the percent operator following its operand
            // it isn't sorted into unary operators for compiler interna.
            lclPushOpCodeMapEntry( aVec, mpTable.get(), ocPercentSign );
            // "+" can be used as unary operator too, push only if binary group is not set
            if ((nGroups & FormulaMapGroup::BINARY_OPERATORS) == 0)
                lclPushOpCodeMapEntry( aVec, mpTable.get(), ocAdd );
            // regular unary operators
            for (sal_uInt16 nOp = SC_OPCODE_START_UN_OP; nOp < SC_OPCODE_STOP_UN_OP && nOp < mnSymbols; ++nOp)
            {
                lclPushOpCodeMapEntry( aVec, mpTable.get(), nOp );
            }
        }
        if ((nGroups & FormulaMapGroup::BINARY_OPERATORS) != 0)
        {
            for (sal_uInt16 nOp = SC_OPCODE_START_BIN_OP; nOp < SC_OPCODE_STOP_BIN_OP && nOp < mnSymbols; ++nOp)
            {
                switch (nOp)
                {
                    // AND and OR in fact are functions but for legacy reasons
                    // are sorted into binary operators for compiler interna.
                    case SC_OPCODE_AND :
                    case SC_OPCODE_OR :
                        break;   // nothing,
                    default:
                        lclPushOpCodeMapEntry( aVec, mpTable.get(), nOp );
                }
            }
        }
        if ((nGroups & FormulaMapGroup::FUNCTIONS) != 0)
        {
            // Function names are not consecutive, skip the gaps between
            // functions with no parameter, functions with 1 parameter
            lclPushOpCodeMapEntries( aVec, mpTable.get(), SC_OPCODE_START_NO_PAR,
                    ::std::min< sal_uInt16 >( SC_OPCODE_STOP_NO_PAR, mnSymbols ) );
            lclPushOpCodeMapEntries( aVec, mpTable.get(), SC_OPCODE_START_1_PAR,
                    ::std::min< sal_uInt16 >( SC_OPCODE_STOP_1_PAR, mnSymbols ) );
            // Additional functions not within range of functions.
            static const sal_uInt16 aOpCodes[] = {
                SC_OPCODE_IF,
                SC_OPCODE_IF_ERROR,
                SC_OPCODE_IF_NA,
                SC_OPCODE_CHOOSE,
                SC_OPCODE_LET,
                SC_OPCODE_AND,
                SC_OPCODE_OR
            };
            lclPushOpCodeMapEntries( aVec, mpTable.get(), aOpCodes, SAL_N_ELEMENTS(aOpCodes) );
            // functions with 2 or more parameters.
            for (sal_uInt16 nOp = SC_OPCODE_START_2_PAR; nOp < SC_OPCODE_STOP_2_PAR && nOp < mnSymbols; ++nOp)
            {
                switch (nOp)
                {
                    // NO_NAME is in SPECIAL.
                    case SC_OPCODE_NO_NAME :
                        break;   // nothing,
                    default:
                        lclPushOpCodeMapEntry( aVec, mpTable.get(), nOp );
                }
            }
            // If AddIn functions are present in this mapping, use them, and only those.
            if (hasExternals())
            {
                for (auto const& elem : maExternalHashMap)
                {
                    FormulaOpCodeMapEntry aEntry;
                    aEntry.Name = elem.first;
                    aEntry.Token.Data <<= elem.second;
                    aEntry.Token.OpCode = ocExternal;
                    aVec.push_back( aEntry);
                }
            }
            else
            {
                rCompiler.fillAddInToken( aVec, isEnglish());
            }
        }
    }
    return uno::Sequence< FormulaOpCodeMapEntry >(aVec.data(), aVec.size());
}


void FormulaCompiler::OpCodeMap::putOpCode( const OUString & rStr, const OpCode eOp, const CharClass* pCharClass )
{
    if (0 < eOp && sal_uInt16(eOp) < mnSymbols)
    {
        bool bPutOp = mpTable[eOp].isEmpty();
        bool bRemoveFromMap = false;
        if (!bPutOp)
        {
            switch (eOp)
            {
                // These OpCodes are meant to overwrite and also remove an
                // existing mapping.
                case ocCurrency:
                    bPutOp = true;
                    bRemoveFromMap = true;
                break;
                // These separator OpCodes are meant to overwrite and also
                // remove an existing mapping if it is not used for one of the
                // other separators.
                case ocArrayColSep:
                    bPutOp = true;
                    bRemoveFromMap = (mpTable[ocArrayRowSep] != mpTable[eOp] && mpTable[ocSep] != mpTable[eOp]);
                break;
                case ocArrayRowSep:
                    bPutOp = true;
                    bRemoveFromMap = (mpTable[ocArrayColSep] != mpTable[eOp] && mpTable[ocSep] != mpTable[eOp]);
                break;
                // For ocSep keep the ";" in map but remove any other if it is
                // not used for ocArrayColSep or ocArrayRowSep.
                case ocSep:
                    bPutOp = true;
                    bRemoveFromMap = (mpTable[eOp] != ";" &&
                            mpTable[ocArrayColSep] != mpTable[eOp] &&
                            mpTable[ocArrayRowSep] != mpTable[eOp]);
                break;
                // These OpCodes are known to be duplicates in the Excel
                // external API mapping because of different parameter counts
                // in different BIFF versions. Names are identical and entries
                // are ignored.
                case ocLinest:
                case ocTrend:
                case ocLogest:
                case ocGrowth:
                case ocTrunc:
                case ocFixed:
                case ocGetDayOfWeek:
                case ocHLookup:
                case ocVLookup:
                case ocGetDiffDate360:
                    if (rStr == mpTable[eOp])
                        return;
                    [[fallthrough]];
                // These OpCodes are known to be added to an existing mapping,
                // but only for the OOXML external API mapping. This is *not*
                // FormulaLanguage::OOXML. Keep the first
                // (correct) definition for the OpCode, all following are
                // additional alias entries in the map.
                case ocErrorType:
                case ocMultiArea:
                case ocBackSolver:
                case ocEasterSunday:
                case ocCurrent:
                case ocStyle:
                    if (mbEnglish &&
                            FormulaGrammar::extractFormulaLanguage( meGrammar) == FormulaGrammar::GRAM_EXTERNAL)
                    {
                        // Both bPutOp and bRemoveFromMap stay false.
                        break;
                    }
                    [[fallthrough]];
                default:
                    SAL_WARN("formula.core",
                            "OpCodeMap::putOpCode: reusing OpCode " << static_cast<sal_uInt16>(eOp)
                            << ", replacing '" << mpTable[eOp] << "' with '" << rStr << "' in "
                            << (mbEnglish ? "" : "non-") << "English map 0x" << ::std::hex << meGrammar);
            }
        }

        // Case preserving opcode -> string, upper string -> opcode
        if (bRemoveFromMap)
        {
            OUString aUpper( pCharClass ? pCharClass->uppercase( mpTable[eOp]) : rStr.toAsciiUpperCase());
            // Ensure we remove a mapping only for the requested OpCode.
            OpCodeHashMap::const_iterator it( maHashMap.find( aUpper));
            if (it != maHashMap.end() && (*it).second == eOp)
                maHashMap.erase( it);
        }
        if (bPutOp)
            mpTable[eOp] = rStr;
        OUString aUpper( pCharClass ? pCharClass->uppercase( rStr) : rStr.toAsciiUpperCase());
        maHashMap.emplace(aUpper, eOp);
    }
    else
    {
        SAL_WARN( "formula.core", "OpCodeMap::putOpCode: OpCode out of range");
    }
}


FormulaCompiler::FormulaCompiler( FormulaTokenArray& rArr, bool bComputeII, bool bMatrixFlag )
        :
        nCurrentFactorParam(0),
        pArr( &rArr ),
        maArrIterator( rArr ),
        pCode( nullptr ),
        pStack( nullptr ),
        eLastOp( ocPush ),
        nRecursion( 0 ),
        nNumFmt( SvNumFormatType::UNDEFINED ),
        pc( 0 ),
        meGrammar( formula::FormulaGrammar::GRAM_UNSPECIFIED ),
        bAutoCorrect( false ),
        bCorrected( false ),
        glSubTotal( false ),
        needsRPNTokenCheck( false ),
        mbJumpCommandReorder(true),
        mbStopOnError(true),
        mbComputeII(bComputeII),
        mbMatrixFlag(bMatrixFlag)
{
}

FormulaTokenArray FormulaCompiler::smDummyTokenArray;

FormulaCompiler::FormulaCompiler(bool bComputeII, bool bMatrixFlag)
        :
        nCurrentFactorParam(0),
        pArr( nullptr ),
        maArrIterator( smDummyTokenArray ),
        pCode( nullptr ),
        pStack( nullptr ),
        eLastOp( ocPush ),
        nRecursion(0),
        nNumFmt( SvNumFormatType::UNDEFINED ),
        pc( 0 ),
        meGrammar( formula::FormulaGrammar::GRAM_UNSPECIFIED ),
        bAutoCorrect( false ),
        bCorrected( false ),
        glSubTotal( false ),
        needsRPNTokenCheck( false ),
        mbJumpCommandReorder(true),
        mbStopOnError(true),
        mbComputeII(bComputeII),
        mbMatrixFlag(bMatrixFlag)
{
}

FormulaCompiler::~FormulaCompiler()
{
}

FormulaCompiler::OpCodeMapPtr FormulaCompiler::GetOpCodeMap( const sal_Int32 nLanguage ) const
{
    const bool bTemporary = !HasOpCodeMap(nLanguage);
    OpCodeMapPtr xMap = GetFinalOpCodeMap(nLanguage);
    if (bTemporary)
        const_cast<FormulaCompiler*>(this)->DestroyOpCodeMap(nLanguage);
    return xMap;
}

FormulaCompiler::OpCodeMapPtr FormulaCompiler::GetFinalOpCodeMap( const sal_Int32 nLanguage ) const
{
    FormulaCompiler::OpCodeMapPtr xMap;
    using namespace sheet;
    switch (nLanguage)
    {
        case FormulaLanguage::ODFF :
            if (!mxSymbolsODFF)
                InitSymbolsODFF( InitSymbols::INIT);
            xMap = mxSymbolsODFF;
            break;
        case FormulaLanguage::ODF_11 :
            if (!mxSymbolsPODF)
                InitSymbolsPODF( InitSymbols::INIT);
            xMap = mxSymbolsPODF;
            break;
        case FormulaLanguage::ENGLISH :
            if (!mxSymbolsEnglish)
                InitSymbolsEnglish( InitSymbols::INIT);
            xMap = mxSymbolsEnglish;
            break;
        case FormulaLanguage::NATIVE :
            if (!mxSymbolsNative)
                InitSymbolsNative( InitSymbols::INIT);
            xMap = mxSymbolsNative;
            break;
        case FormulaLanguage::XL_ENGLISH:
            if (!mxSymbolsEnglishXL)
                InitSymbolsEnglishXL( InitSymbols::INIT);
            xMap = mxSymbolsEnglishXL;
            break;
        case FormulaLanguage::OOXML:
            if (!mxSymbolsOOXML)
                InitSymbolsOOXML( InitSymbols::INIT);
            xMap = mxSymbolsOOXML;
            break;
        case FormulaLanguage::API :
            if (!mxSymbolsAPI)
                InitSymbolsAPI( InitSymbols::INIT);
            xMap = mxSymbolsAPI;
            break;
        default:
            ;   // nothing, NULL map returned
    }
    return xMap;
}

void FormulaCompiler::DestroyOpCodeMap( const sal_Int32 nLanguage )
{
    using namespace sheet;
    switch (nLanguage)
    {
        case FormulaLanguage::ODFF :
            InitSymbolsODFF( InitSymbols::DESTROY);
            break;
        case FormulaLanguage::ODF_11 :
            InitSymbolsPODF( InitSymbols::DESTROY);
            break;
        case FormulaLanguage::ENGLISH :
            InitSymbolsEnglish( InitSymbols::DESTROY);
            break;
        case FormulaLanguage::NATIVE :
            InitSymbolsNative( InitSymbols::DESTROY);
            break;
        case FormulaLanguage::XL_ENGLISH:
            InitSymbolsEnglishXL( InitSymbols::DESTROY);
            break;
        case FormulaLanguage::OOXML:
            InitSymbolsOOXML( InitSymbols::DESTROY);
            break;
        case FormulaLanguage::API :
            InitSymbolsAPI( InitSymbols::DESTROY);
            break;
        default:
            ;   // nothing
    }
}

bool FormulaCompiler::HasOpCodeMap( const sal_Int32 nLanguage ) const
{
    using namespace sheet;
    switch (nLanguage)
    {
        case FormulaLanguage::ODFF :
            return InitSymbolsODFF( InitSymbols::ASK);
        case FormulaLanguage::ODF_11 :
            return InitSymbolsPODF( InitSymbols::ASK);
        case FormulaLanguage::ENGLISH :
            return InitSymbolsEnglish( InitSymbols::ASK);
        case FormulaLanguage::NATIVE :
            return InitSymbolsNative( InitSymbols::ASK);
        case FormulaLanguage::XL_ENGLISH:
            return InitSymbolsEnglishXL( InitSymbols::ASK);
        case FormulaLanguage::OOXML:
            return InitSymbolsOOXML( InitSymbols::ASK);
        case FormulaLanguage::API :
            return InitSymbolsAPI( InitSymbols::ASK);
        default:
            ;   // nothing
    }
    return false;
}

OUString FormulaCompiler::FindAddInFunction( const OUString& /*rUpperName*/, bool /*bLocalFirst*/ ) const
{
    return OUString();
}

FormulaCompiler::OpCodeMapPtr FormulaCompiler::CreateOpCodeMap(
        const uno::Sequence<
        const sheet::FormulaOpCodeMapEntry > & rMapping,
        bool bEnglish )
{
    using sheet::FormulaOpCodeMapEntry;
    // Filter / API maps are never Core
    NonConstOpCodeMapPtr xMap = std::make_shared<OpCodeMap>( SC_OPCODE_LAST_OPCODE_ID + 1, false,
                FormulaGrammar::mergeToGrammar( FormulaGrammar::setEnglishBit(
                        FormulaGrammar::GRAM_EXTERNAL, bEnglish), FormulaGrammar::CONV_UNSPECIFIED));
    std::unique_ptr<CharClass> xCharClass( xMap->isEnglish() ? nullptr : createCharClassIfNonEnglishUI());
    const CharClass* pCharClass = xCharClass.get();
    for (auto const& rMapEntry : rMapping)
    {
        OpCode eOp = OpCode(rMapEntry.Token.OpCode);
        if (eOp != ocExternal)
            xMap->putOpCode( rMapEntry.Name, eOp, pCharClass);
        else
        {
            OUString aExternalName;
            if (rMapEntry.Token.Data >>= aExternalName)
                xMap->putExternal( rMapEntry.Name, aExternalName);
            else
            {
                SAL_WARN( "formula.core", "FormulaCompiler::CreateOpCodeMap: no Token.Data external name");
            }
        }
    }
    return xMap;
}

static bool lcl_fillNativeSymbols( FormulaCompiler::NonConstOpCodeMapPtr& xMap, FormulaCompiler::InitSymbols eWhat = FormulaCompiler::InitSymbols::INIT )
{
    static OpCodeMapData aSymbolMap;
    static std::map<OUString, OpCodeMapData> aLocaleSymbolMap;
    std::unique_lock aGuard(aSymbolMap.maMtx);

    if (comphelper::LibreOfficeKit::isActive())
    {
        OUString language = comphelper::LibreOfficeKit::getLanguageTag().getLanguage();
        if (eWhat == FormulaCompiler::InitSymbols::ASK)
        {
            return aLocaleSymbolMap.contains(language)
                   && bool(aLocaleSymbolMap[language].mxSymbolMap);
        }
        else if (eWhat == FormulaCompiler::InitSymbols::DESTROY)
        {
            aLocaleSymbolMap[language].mxSymbolMap.reset();
        }
        else if (!aLocaleSymbolMap[language].mxSymbolMap)
        {
            // Core
            aLocaleSymbolMap[language].mxSymbolMap = std::make_shared<FormulaCompiler::OpCodeMap>(
                SC_OPCODE_LAST_OPCODE_ID + 1, true, FormulaGrammar::GRAM_NATIVE_UI);
            OpCodeList aOpCodeListSymbols(RID_STRLIST_FUNCTION_NAMES_SYMBOLS,
                                          aLocaleSymbolMap[language].mxSymbolMap);
            OpCodeList aOpCodeListNative(RID_STRLIST_FUNCTION_NAMES,
                                         aLocaleSymbolMap[language].mxSymbolMap);
            // No AddInMap for native core mapping.
        }

        xMap = aLocaleSymbolMap[language].mxSymbolMap;
    }
    else
    {
        if (eWhat == FormulaCompiler::InitSymbols::ASK)
        {
            return bool(aSymbolMap.mxSymbolMap);
        }
        else if (eWhat == FormulaCompiler::InitSymbols::DESTROY)
        {
            aSymbolMap.mxSymbolMap.reset();
        }
        else if (!aSymbolMap.mxSymbolMap)
        {
            // Core
            aSymbolMap.mxSymbolMap = std::make_shared<FormulaCompiler::OpCodeMap>(
                SC_OPCODE_LAST_OPCODE_ID + 1, true, FormulaGrammar::GRAM_NATIVE_UI);
            OpCodeList aOpCodeListSymbols(RID_STRLIST_FUNCTION_NAMES_SYMBOLS,
                                          aSymbolMap.mxSymbolMap);
            OpCodeList aOpCodeListNative(RID_STRLIST_FUNCTION_NAMES, aSymbolMap.mxSymbolMap);
            // No AddInMap for native core mapping.
        }

        xMap = aSymbolMap.mxSymbolMap;
    }

    return true;
}

const OUString& FormulaCompiler::GetNativeSymbol( OpCode eOp )
{
    NonConstOpCodeMapPtr xSymbolsNative;
    lcl_fillNativeSymbols( xSymbolsNative);
    return xSymbolsNative->getSymbol( eOp );
}

sal_Unicode FormulaCompiler::GetNativeSymbolChar( OpCode eOp )
{
    return GetNativeSymbol(eOp)[0];
}

bool FormulaCompiler::InitSymbolsNative( FormulaCompiler::InitSymbols eWhat ) const
{
    return lcl_fillNativeSymbols( mxSymbolsNative, eWhat);
}

bool FormulaCompiler::InitSymbolsEnglish( FormulaCompiler::InitSymbols eWhat ) const
{
    static OpCodeMapData aMap;
    std::unique_lock aGuard(aMap.maMtx);
    if (eWhat == InitSymbols::ASK)
        return bool(aMap.mxSymbolMap);
    else if (eWhat == InitSymbols::DESTROY)
        aMap.mxSymbolMap.reset();
    else if (!aMap.mxSymbolMap)
        loadSymbols(RID_STRLIST_FUNCTION_NAMES_ENGLISH, FormulaGrammar::GRAM_ENGLISH, aMap.mxSymbolMap);
    mxSymbolsEnglish = aMap.mxSymbolMap;
    return true;
}

bool FormulaCompiler::InitSymbolsPODF( FormulaCompiler::InitSymbols eWhat ) const
{
    static OpCodeMapData aMap;
    std::unique_lock aGuard(aMap.maMtx);
    if (eWhat == InitSymbols::ASK)
        return bool(aMap.mxSymbolMap);
    else if (eWhat == InitSymbols::DESTROY)
        aMap.mxSymbolMap.reset();
    else if (!aMap.mxSymbolMap)
        loadSymbols(RID_STRLIST_FUNCTION_NAMES_ENGLISH_PODF, FormulaGrammar::GRAM_PODF, aMap.mxSymbolMap, SeparatorType::RESOURCE_BASE);
    mxSymbolsPODF = aMap.mxSymbolMap;
    return true;
}

bool FormulaCompiler::InitSymbolsAPI( FormulaCompiler::InitSymbols eWhat ) const
{
    static OpCodeMapData aMap;
    std::unique_lock aGuard(aMap.maMtx);
    if (eWhat == InitSymbols::ASK)
        return bool(aMap.mxSymbolMap);
    else if (eWhat == InitSymbols::DESTROY)
        aMap.mxSymbolMap.reset();
    else if (!aMap.mxSymbolMap)
        loadSymbols(RID_STRLIST_FUNCTION_NAMES_ENGLISH_API, FormulaGrammar::GRAM_API, aMap.mxSymbolMap, SeparatorType::RESOURCE_BASE);
    mxSymbolsAPI = aMap.mxSymbolMap;
    return true;
}

bool FormulaCompiler::InitSymbolsODFF( FormulaCompiler::InitSymbols eWhat ) const
{
    static OpCodeMapData aMap;
    std::unique_lock aGuard(aMap.maMtx);
    if (eWhat == InitSymbols::ASK)
        return bool(aMap.mxSymbolMap);
    else if (eWhat == InitSymbols::DESTROY)
        aMap.mxSymbolMap.reset();
    else if (!aMap.mxSymbolMap)
        loadSymbols(RID_STRLIST_FUNCTION_NAMES_ENGLISH_ODFF, FormulaGrammar::GRAM_ODFF, aMap.mxSymbolMap, SeparatorType::RESOURCE_BASE);
    mxSymbolsODFF = aMap.mxSymbolMap;
    return true;
}

bool FormulaCompiler::InitSymbolsEnglishXL( FormulaCompiler::InitSymbols eWhat ) const
{
    static OpCodeMapData aMap;
    std::unique_lock aGuard(aMap.maMtx);
    if (eWhat == InitSymbols::ASK)
        return bool(aMap.mxSymbolMap);
    else if (eWhat == InitSymbols::DESTROY)
        aMap.mxSymbolMap.reset();
    else if (!aMap.mxSymbolMap)
        loadSymbols(RID_STRLIST_FUNCTION_NAMES_ENGLISH, FormulaGrammar::GRAM_ENGLISH, aMap.mxSymbolMap);
    mxSymbolsEnglishXL = aMap.mxSymbolMap;
    if (eWhat != InitSymbols::INIT)
        return true;

    // TODO: For now, just replace the separators to the Excel English
    // variants. Later, if we want to properly map Excel functions with Calc
    // functions, we'll need to do a little more work here.
    mxSymbolsEnglishXL->putOpCode( OUString(','), ocSep, nullptr);
    mxSymbolsEnglishXL->putOpCode( OUString(','), ocArrayColSep, nullptr);
    mxSymbolsEnglishXL->putOpCode( OUString(';'), ocArrayRowSep, nullptr);

    return true;
}

bool FormulaCompiler::InitSymbolsOOXML( FormulaCompiler::InitSymbols eWhat ) const
{
    static OpCodeMapData aMap;
    std::unique_lock aGuard(aMap.maMtx);
    if (eWhat == InitSymbols::ASK)
        return bool(aMap.mxSymbolMap);
    else if (eWhat == InitSymbols::DESTROY)
        aMap.mxSymbolMap.reset();
    else if (!aMap.mxSymbolMap)
        loadSymbols(RID_STRLIST_FUNCTION_NAMES_ENGLISH_OOXML, FormulaGrammar::GRAM_OOXML, aMap.mxSymbolMap, SeparatorType::RESOURCE_BASE);
    mxSymbolsOOXML = aMap.mxSymbolMap;
    return true;
}


void FormulaCompiler::loadSymbols(const std::pair<const char*, int>* pSymbols, FormulaGrammar::Grammar eGrammar,
        NonConstOpCodeMapPtr& rxMap, SeparatorType eSepType) const
{
    if ( rxMap )
        return;

    // not Core
    rxMap = std::make_shared<OpCodeMap>( SC_OPCODE_LAST_OPCODE_ID + 1, eGrammar != FormulaGrammar::GRAM_ODFF, eGrammar );
    OpCodeList aOpCodeList(pSymbols, rxMap, eSepType);

    fillFromAddInMap( rxMap, eGrammar);
    // Fill from collection for AddIns not already present.
    if (FormulaGrammar::GRAM_ENGLISH == eGrammar)
        fillFromAddInCollectionEnglishName( rxMap);
    else
    {
        fillFromAddInCollectionUpperName( rxMap);
        if (FormulaGrammar::GRAM_API == eGrammar)
        {
            // Add known but not in AddInMap English names, e.g. from the
            // PricingFunctions AddIn or any user supplied AddIn.
            fillFromAddInCollectionEnglishName( rxMap);
        }
        else if (FormulaGrammar::GRAM_OOXML == eGrammar)
        {
            // Add specified Add-In compatibility name.
            fillFromAddInCollectionExcelName( rxMap);
        }
    }
}

void FormulaCompiler::fillFromAddInCollectionUpperName( const NonConstOpCodeMapPtr& /*xMap */) const
{
}

void FormulaCompiler::fillFromAddInCollectionEnglishName( const NonConstOpCodeMapPtr& /*xMap */) const
{
}

void FormulaCompiler::fillFromAddInCollectionExcelName( const NonConstOpCodeMapPtr& /*xMap */) const
{
}

void FormulaCompiler::fillFromAddInMap( const NonConstOpCodeMapPtr& /*xMap*/, FormulaGrammar::Grammar /*_eGrammar */) const
{
}

OpCode FormulaCompiler::GetEnglishOpCode( const OUString& rName ) const
{
    FormulaCompiler::OpCodeMapPtr xMap = GetOpCodeMap( sheet::FormulaLanguage::ENGLISH);

    formula::OpCodeHashMap::const_iterator iLook( xMap->getHashMap().find( rName ) );
    bool bFound = (iLook != xMap->getHashMap().end());
    return bFound ? (*iLook).second : ocNone;
}

bool FormulaCompiler::IsOpCodeVolatile( OpCode eOp )
{
    bool bRet = false;
    switch (eOp)
    {
        // no parameters:
        case ocRandom:
        case ocGetActDate:
        case ocGetActTime:
        // one parameter:
        case ocFormula:
        case ocInfo:
        // more than one parameters:
            // ocIndirect otherwise would have to do
            // StopListening and StartListening on a reference for every
            // interpreted value.
        case ocIndirect:
            // ocOffset results in indirect references.
        case ocOffset:
            // ocDebugVar shows internal value that may change as the internal state changes.
        case ocDebugVar:
            // ocRandArray is a volatile function.
        case ocRandArray:
            bRet = true;
            break;
        default:
            bRet = false;
            break;
    }
    return bRet;
}

bool FormulaCompiler::IsOpCodeJumpCommand( OpCode eOp )
{
    switch (eOp)
    {
        case ocIf:
        case ocIfError:
        case ocIfNA:
        case ocChoose:
        case ocLet:
            return true;
        default:
            ;
    }
    return false;
}

// Remove quotes, escaped quotes are unescaped.
bool FormulaCompiler::DeQuote( OUString& rStr )
{
    sal_Int32 nLen = rStr.getLength();
    if ( nLen > 1 && rStr[0] == '\'' && rStr[ nLen-1 ] == '\'' )
    {
        rStr = rStr.copy( 1, nLen-2 );
        rStr = rStr.replaceAll( "''", "'" );
        return true;
    }
    return false;
}

void FormulaCompiler::fillAddInToken(
        ::std::vector< sheet::FormulaOpCodeMapEntry >& /*_rVec*/,
        bool /*_bIsEnglish*/) const
{
}

bool FormulaCompiler::IsMatrixFunction( OpCode eOpCode )
{
    switch (eOpCode)
    {
        case ocDde :
        case ocGrowth :
        case ocTrend :
        case ocLogest :
        case ocLinest :
        case ocFrequency :
        case ocMatSequence :
        case ocMatTrans :
        case ocMatMult :
        case ocMatInv :
        case ocMatrixUnit :
        case ocModalValue_Multi :
        case ocFourier :
        case ocFilter :
        case ocSort :
        case ocSortBy :
        case ocRandArray :
        case ocChooseCols :
        case ocChooseRows :
        case ocDrop :
        case ocExpand :
        case ocHStack :
        case ocVStack :
        case ocTake :
        case ocTextSplit :
        case ocToCol :
        case ocToRow :
        case ocUnique :
        case ocLet :
        case ocWrapCols :
        case ocWrapRows :
            return true;
        default:
        {
            // added to avoid warnings
        }
    }
    return false;
}


void FormulaCompiler::OpCodeMap::putCopyOpCode( const OUString& rSymbol, OpCode eOp, const CharClass* pCharClass )
{
    SAL_WARN_IF( !mpTable[eOp].isEmpty() && rSymbol.isEmpty(), "formula.core",
            "OpCodeMap::putCopyOpCode: NOT replacing OpCode " << static_cast<sal_uInt16>(eOp)
            << " '" << mpTable[eOp] << "' with empty name!");
    if (!mpTable[eOp].isEmpty() && rSymbol.isEmpty())
    {
        OUString aUpper( pCharClass ? pCharClass->uppercase( mpTable[eOp]) : mpTable[eOp].toAsciiUpperCase());
        maHashMap.emplace(aUpper, eOp);
    }
    else
    {
        OUString aUpper( pCharClass ? pCharClass->uppercase( rSymbol) : rSymbol.toAsciiUpperCase());
        mpTable[eOp] = rSymbol;
        maHashMap.emplace(aUpper, eOp);
    }
}

void FormulaCompiler::OpCodeMap::copyFrom( const OpCodeMap& r )
{
    maHashMap = OpCodeHashMap( mnSymbols);

    sal_uInt16 n = r.getSymbolCount();
    SAL_WARN_IF( n != mnSymbols, "formula.core",
            "OpCodeMap::copyFrom: unequal size, this: " << mnSymbols << "  that: " << n);
    if (n > mnSymbols)
        n = mnSymbols;

    // OpCode 0 (ocPush) should never be in a map.
    SAL_WARN_IF( !mpTable[0].isEmpty() || !r.mpTable[0].isEmpty(), "formula.core",
            "OpCodeMap::copyFrom: OpCode 0 assigned, this: '"
            << mpTable[0] << "'  that: '" << r.mpTable[0] << "'");

    std::unique_ptr<CharClass> xCharClass( r.mbEnglish ? nullptr : createCharClassIfNonEnglishUI());
    const CharClass* pCharClass = xCharClass.get();

    // For bOverrideKnownBad when copying from the English core map (ODF 1.1
    // and API) to the native map (UI "use English function names") replace the
    // known bad legacy function names with correct ones.
    if (r.mbCore &&
            FormulaGrammar::extractFormulaLanguage( meGrammar) == sheet::FormulaLanguage::NATIVE &&
            FormulaGrammar::extractFormulaLanguage( r.meGrammar) == sheet::FormulaLanguage::ENGLISH)
    {
        for (sal_uInt16 i = 1; i < n; ++i)
        {
            OUString aSymbol;
            OpCode eOp = OpCode(i);
            switch (eOp)
            {
                case ocRRI:
                    aSymbol = "RRI";
                    break;
                case ocTableOp:
                    aSymbol = "MULTIPLE.OPERATIONS";
                    break;
                default:
                    aSymbol = r.mpTable[i];
            }
            putCopyOpCode( aSymbol, eOp, pCharClass);
        }
    }
    else
    {
        for (sal_uInt16 i = 1; i < n; ++i)
        {
            OpCode eOp = OpCode(i);
            const OUString& rSymbol = r.mpTable[i];
            putCopyOpCode( rSymbol, eOp, pCharClass);
        }
    }

    // This was meant to copy to native map that does not have AddIn symbols
    // but needs them from the source map. It is unclear what should happen if
    // the destination already had externals, so do it only if it doesn't.
    if (!hasExternals())
    {
        maExternalHashMap = r.maExternalHashMap;
        maReverseExternalHashMap = r.maReverseExternalHashMap;
        mbCore = r.mbCore;
        if (mbEnglish != r.mbEnglish)
        {
            // For now keep mbEnglishLocale setting, which is false for a
            // non-English native map we're copying to.
            /* TODO:
            if (!mbEnglish && r.mbEnglish)
                mbEnglishLocale = "getUseEnglishLocaleFromConfiguration()";
            or set from outside i.e. via ScCompiler.
            */
            mbEnglish = r.mbEnglish;
        }
    }
}


FormulaError FormulaCompiler::GetErrorConstant( const OUString& rName ) const
{
    FormulaError nError = FormulaError::NONE;
    OpCodeHashMap::const_iterator iLook( mxSymbols->getHashMap().find( rName));
    if (iLook != mxSymbols->getHashMap().end())
    {
        switch ((*iLook).second)
        {
            // Not all may make sense in a formula, but these we know as
            // opcodes.
            case ocErrNull:
                nError = FormulaError::NoCode;
                break;
            case ocErrDivZero:
                nError = FormulaError::DivisionByZero;
                break;
            case ocErrValue:
                nError = FormulaError::NoValue;
                break;
            case ocErrRef:
                nError = FormulaError::NoRef;
                break;
            case ocErrName:
                nError = FormulaError::NoName;
                break;
            case ocErrNum:
                nError = FormulaError::IllegalFPOperation;
                break;
            case ocErrNA:
                nError = FormulaError::NotAvailable;
                break;
            default:
                ;   // nothing
        }
    }
    else
    {
        // Per convention recognize detailed "#ERRxxx!" constants, always
        // untranslated. Error numbers are sal_uInt16 so at most 5 decimal
        // digits.
        if (rName.startsWithIgnoreAsciiCase("#ERR") && rName.getLength() <= 10 && rName[rName.getLength()-1] == '!')
        {
            sal_uInt32 nErr = o3tl::toUInt32(rName.subView( 4, rName.getLength() - 5));
            if (0 < nErr && nErr <= SAL_MAX_UINT16 && isPublishedFormulaError(static_cast<FormulaError>(nErr)))
                nError = static_cast<FormulaError>(nErr);
        }
    }
    return nError;
}

void FormulaCompiler::EnableJumpCommandReorder( bool bEnable )
{
    mbJumpCommandReorder = bEnable;
}

void FormulaCompiler::EnableStopOnError( bool bEnable )
{
    mbStopOnError = bEnable;
}

void FormulaCompiler::AppendErrorConstant( OUStringBuffer& rBuffer, FormulaError nError ) const
{
    OpCode eOp;
    switch (nError)
    {
        case FormulaError::NoCode:
            eOp = ocErrNull;
            break;
        case FormulaError::DivisionByZero:
            eOp = ocErrDivZero;
            break;
        case FormulaError::NoValue:
            eOp = ocErrValue;
            break;
        case FormulaError::NoRef:
            eOp = ocErrRef;
            break;
        case FormulaError::NoName:
            eOp = ocErrName;
            break;
        case FormulaError::IllegalFPOperation:
            eOp = ocErrNum;
            break;
        case FormulaError::NotAvailable:
            eOp = ocErrNA;
            break;
        default:
            {
                // Per convention create detailed "#ERRxxx!" constants, always
                // untranslated.
                rBuffer.append("#ERR");
                rBuffer.append(static_cast<sal_Int32>(nError));
                rBuffer.append('!');
                return;
            }
    }
    rBuffer.append( mxSymbols->getSymbol( eOp));
}

constexpr short nRecursionMax = 100;

bool FormulaCompiler::GetToken()
{
    FormulaCompilerRecursionGuard aRecursionGuard( nRecursion );
    if ( nRecursion > nRecursionMax )
    {
        SetError( FormulaError::StackOverflow );
        mpLastToken = mpToken = new FormulaByteToken( ocStop );
        return false;
    }
    if ( bAutoCorrect && !pStack )
    {   // don't merge stacked subroutine code into entered formula
        aCorrectedFormula += aCorrectedSymbol;
        aCorrectedSymbol.clear();
    }
    bool bStop = false;
    if (pArr->GetCodeError() != FormulaError::NONE && mbStopOnError)
        bStop = true;
    else
    {
        FormulaTokenRef pSpacesToken;
        short nWasColRowName;
        if ( pArr->OpCodeBefore( maArrIterator.GetIndex() ) == ocColRowName )
             nWasColRowName = 1;
        else
             nWasColRowName = 0;
        OpCode eTmpOp;
        mpToken = maArrIterator.Next();
        while (mpToken && ((eTmpOp = mpToken->GetOpCode()) == ocSpaces || eTmpOp == ocWhitespace))
        {
            if (eTmpOp == ocSpaces)
            {
                // For significant whitespace remember last ocSpaces token.
                // Usually there's only one even for multiple spaces.
                pSpacesToken = mpToken;
                if ( nWasColRowName )
                    nWasColRowName++;
            }
            if ( bAutoCorrect && !pStack )
                CreateStringFromToken( aCorrectedFormula, mpToken.get() );
            mpToken = maArrIterator.Next();
        }
        if ( bAutoCorrect && !pStack && mpToken )
            CreateStringFromToken( aCorrectedSymbol, mpToken.get() );
        if( !mpToken )
        {
            if( pStack )
            {
                PopTokenArray();
                // mpLastToken was popped as well and corresponds to the
                // then current last token during PushTokenArray(), e.g. for
                // HandleRange().
                return GetToken();
            }
            else
                bStop = true;
        }
        else
        {
            if ( nWasColRowName >= 2 && mpToken->GetOpCode() == ocColRowName )
            {   // convert an ocSpaces to ocIntersect in RPN
                mpLastToken = mpToken = new FormulaByteToken( ocIntersect );
                maArrIterator.StepBack();     // we advanced to the second ocColRowName, step back
            }
            else if (pSpacesToken && FormulaGrammar::isExcelSyntax( meGrammar) &&
                    mpLastToken && mpToken &&
                    isPotentialRangeType( mpToken.get(), false, true) &&
                    (mpLastToken->GetOpCode() == ocClose || isPotentialRangeType( mpLastToken.get(), false, false)))
            {
                // Let IntersectionLine() <- Factor() decide how to treat this,
                // once the actual arguments are determined in RPN.
                mpLastToken = mpToken = std::move(pSpacesToken);
                maArrIterator.StepBack();     // step back from next non-spaces token
                return true;
            }
        }
    }
    if( bStop )
    {
        mpLastToken = mpToken = new FormulaByteToken( ocStop );
        return false;
    }

    // Remember token for next round and any PushTokenArray() calls that may
    // occur in handlers.
    mpLastToken = mpToken;

    if ( mpToken->IsExternalRef() )
    {
        return HandleExternalReference(*mpToken);
    }
    else
    {
        switch (mpToken->GetOpCode())
        {
            case ocSubTotal:
            case ocAggregate:
                glSubTotal = true;
                break;
            case ocStringName:
                if( HandleStringName())
                    return true;
                else
                    return false;
            case ocName:
                if( HandleRange())
                {
                    // Expanding ocName might have introduced tokens such as ocStyle that prevent formula threading,
                    // but those wouldn't be present in the raw tokens array, so ensure RPN tokens will be checked too.
                    needsRPNTokenCheck = true;
                    return true;
                }
                return false;
            case ocColRowName:
                return HandleColRowName();
            case ocDBArea:
                return HandleDbData();
            case ocTableRef:
                return HandleTableRef();
            case ocPush:
                if( mbComputeII )
                    HandleIIOpCode(mpToken.get(), nullptr, 0);
                break;
            default:
                ;   // nothing
        }
    }
    return true;
}


// RPN creation by recursion
void FormulaCompiler::Factor()
{
    if (pArr->GetCodeError() != FormulaError::NONE && mbStopOnError)
        return;

    CurrentFactor pFacToken( this );

    OpCode eOp = mpToken->GetOpCode();
    if (eOp == ocPush || eOp == ocColRowNameAuto || eOp == ocMatRef || eOp == ocDBArea
        || eOp == ocTableRef
        || (!mbJumpCommandReorder && ((eOp == ocName) || (eOp == ocColRowName) || (eOp == ocBad)))
       )
    {
        PutCode( mpToken );
        eOp = NextToken();
        if( eOp == ocOpen )
        {
            // PUSH( is an error that may be caused by an unknown function.
            SetError(
                ( mpToken->GetType() == svString
               || mpToken->GetType() == svSingleRef )
               ? FormulaError::NoName : FormulaError::OperatorExpected );
            if ( bAutoCorrect && !pStack )
            {   // assume multiplication
                aCorrectedFormula += mxSymbols->getSymbol( ocMul);
                bCorrected = true;
                NextToken();
                eOp = Expression();
                if( eOp != ocClose )
                    SetError( FormulaError::PairExpected);
                else
                    NextToken();
            }
        }
    }
    else if( eOp == ocOpen )
    {
        NextToken();
        eOp = Expression();
        while ((eOp == ocSep) && (pArr->GetCodeError() == FormulaError::NONE || !mbStopOnError))
        {   // range list  (A1;A2)  converted to  (A1~A2)
            pFacToken = mpToken;
            NextToken();
            CheckSetForceArrayParameter( mpToken, 0);
            eOp = Expression();
            // Do not ignore error here, regardless of mbStopOnError, to not
            // change the formula expression in case of an unexpected state.
            if (pArr->GetCodeError() == FormulaError::NONE && pc >= 2)
            {
                // Left and right operands must be reference or function
                // returning reference to form a range list.
                const FormulaToken* p = pCode[-2];
                if (p && isPotentialRangeType( p, true, false))
                {
                    p = pCode[-1];
                    if (p && isPotentialRangeType( p, true, true))
                    {
                        pFacToken->NewOpCode( ocUnion, FormulaToken::PrivateAccess());
                        // XXX NOTE: the token's eType is still svSep here!
                        PutCode( pFacToken);
                    }
                }
            }
        }
        if (eOp != ocClose)
            SetError( FormulaError::PairExpected);
        else
            NextToken();

        /* TODO: if no conversion to ocUnion is involved this could collect
         * such expression as a list or (matrix) vector to be passed as
         * argument for one parameter (which in fact the ocUnion svRefList is a
         * special case of), which would require a new StackVar type and needed
         * to be handled by the interpreter for functions that could support it
         * (i.e. already handle VAR_ARGS or svRefList parameters). This is also
         * not defined by ODF.
         * Does Excel handle =SUM((1;2))?
         * As is, the interpreter catches extraneous uncalculated
         * subexpressions like 1 of (1;2) as error. */
    }
    else
    {
        if( nNumFmt == SvNumFormatType::UNDEFINED )
            nNumFmt = lcl_GetRetFormat( eOp );

        if ( IsOpCodeVolatile( eOp) )
            pArr->SetExclusiveRecalcModeAlways();
        else
        {
            switch( eOp )
            {
                    // Functions recalculated on every document load.
                    // ONLOAD_LENIENT here to be able to distinguish and not
                    // force a recalc (if not in an ALWAYS or ONLOAD_MUST
                    // context) but keep an imported result from for example
                    // OOXML a DDE call. Will be recalculated for ODFF.
                case ocConvertOOo :
                case ocDde:
                case ocMacro:
                case ocWebservice:
                    pArr->AddRecalcMode( ScRecalcMode::ONLOAD_LENIENT );
                break;
                    // RANDBETWEEN() is volatile like RAND(). Other Add-In
                    // functions may have to be recalculated or not, we don't
                    // know, classify as ONLOAD_LENIENT.
                case ocExternal:
                    if (mpToken->GetExternal() == "com.sun.star.sheet.addin.Analysis.getRandbetween")
                        pArr->SetExclusiveRecalcModeAlways();
                    else
                        pArr->AddRecalcMode( ScRecalcMode::ONLOAD_LENIENT );
                break;
                    // If the referred cell is moved the value changes.
                case ocColumn :
                case ocRow :
                    pArr->SetRecalcModeOnRefMove();
                break;
                    // ocCell needs recalc on move for some possible type values.
                    // And recalc mode on load, tdf#60645
                case ocCell :
                    pArr->SetRecalcModeOnRefMove();
                    pArr->AddRecalcMode( ScRecalcMode::ONLOAD_MUST );
                break;
                case ocHyperLink :
                    // Cell with hyperlink needs to be calculated on load to
                    // get its matrix result generated.
                    pArr->AddRecalcMode( ScRecalcMode::ONLOAD_MUST );
                    pArr->SetHyperLink( true);
                break;
                default:
                    ;   // nothing
            }
        }
        if (SC_OPCODE_START_NO_PAR <= eOp && eOp < SC_OPCODE_STOP_NO_PAR)
        {
            pFacToken = mpToken;
            eOp = NextToken();
            if (eOp != ocOpen)
            {
                SetError( FormulaError::PairExpected);
                PutCode( pFacToken );
            }
            else
            {
                eOp = NextToken();
                if (eOp != ocClose)
                    SetError( FormulaError::PairExpected);
                PutCode( pFacToken);
                NextToken();
            }
        }
        else if (SC_OPCODE_START_1_PAR <= eOp && eOp < SC_OPCODE_STOP_1_PAR)
        {
            if (eOp == ocIsoWeeknum && FormulaGrammar::isODFF( meGrammar ))
            {
                // tdf#50950 ocIsoWeeknum can have 2 arguments when saved by older versions of Calc;
                // the opcode then has to be changed to ocWeek for backward compatibility
                pFacToken = mpToken;
                eOp = NextToken();
                bool bNoParam = false;
                if (eOp == ocOpen)
                {
                    eOp = NextToken();
                    if (eOp == ocClose)
                        bNoParam = true;
                    else
                    {
                        CheckSetForceArrayParameter( mpToken, 0);
                        eOp = Expression();
                    }
                }
                else
                    SetError( FormulaError::PairExpected);
                sal_uInt32 nSepCount = 0;
                const sal_uInt16 nSepPos = maArrIterator.GetIndex() - 1;    // separator position, if any
                if( !bNoParam )
                {
                    nSepCount++;
                    while ((eOp == ocSep) && (pArr->GetCodeError() == FormulaError::NONE || !mbStopOnError))
                    {
                        NextToken();
                        CheckSetForceArrayParameter( mpToken, nSepCount);
                        nSepCount++;
                        if (nSepCount > FORMULA_MAXPARAMS)
                            SetError( FormulaError::CodeOverflow);
                        eOp = Expression();
                    }
                }
                if (eOp != ocClose)
                    SetError( FormulaError::PairExpected);
                else
                    NextToken();
                pFacToken->SetByte( nSepCount );
                if (nSepCount == 2)
                {
                    // An old mode!=1 indicates ISO week, remove argument if
                    // literal double value and keep function. Anything else
                    // can not be resolved, there exists no "like ISO but week
                    // starts on Sunday" mode in WEEKNUM and for an expression
                    // we can't determine.
                    // Current index is nSepPos+3 if expression stops, or
                    // nSepPos+4 if expression continues after the call because
                    // we just called NextToken() to move away from it.
                    if (pc >= 2 && (maArrIterator.GetIndex() == nSepPos + 3 || maArrIterator.GetIndex() == nSepPos + 4) &&
                            pArr->TokenAt(nSepPos+1)->GetType() == svDouble &&
                            pArr->TokenAt(nSepPos+1)->GetDouble() != 1.0 &&
                            pArr->TokenAt(nSepPos+2)->GetOpCode() == ocClose &&
                            pArr->RemoveToken( nSepPos, 2) == 2)
                    {
                        maArrIterator.AfterRemoveToken( nSepPos, 2);
                        // Remove the ocPush/svDouble just removed also from
                        // the compiler local RPN array.
                        --pCode; --pc;
                        (*pCode)->DecRef(); // may be dead now
                        pFacToken->SetByte( nSepCount - 1 );
                    }
                    else
                    {
                        // For the remaining two arguments cases use the
                        // compatibility function.
                        pFacToken->NewOpCode( ocWeeknumOOo, FormulaToken::PrivateAccess());
                    }
                }
                PutCode( pFacToken );
            }
            else
            {
                // standard handling of 1-parameter opcodes
                pFacToken = mpToken;
                eOp = NextToken();
                if( nNumFmt == SvNumFormatType::UNDEFINED && eOp == ocNot )
                    nNumFmt = SvNumFormatType::LOGICAL;
                if (eOp == ocOpen)
                {
                    NextToken();
                    CheckSetForceArrayParameter( mpToken, 0);
                    eOp = Expression();
                }
                else
                    SetError( FormulaError::PairExpected);
                if (eOp != ocClose)
                    SetError( FormulaError::PairExpected);
                else if ( pArr->GetCodeError() == FormulaError::NONE )
                {
                    pFacToken->SetByte( 1 );
                    if (mbComputeII)
                    {
                        FormulaToken** pArg = pCode - 1;
                        HandleIIOpCode(pFacToken, &pArg, 1);
                    }
                }
                PutCode( pFacToken );
                NextToken();
            }
        }
        else if ((SC_OPCODE_START_2_PAR <= eOp && eOp < SC_OPCODE_STOP_2_PAR)
                || eOp == ocExternal
                || eOp == ocMacro
                || eOp == ocAnd
                || eOp == ocOr
                || eOp == ocBad
                || ( eOp >= ocInternalBegin && eOp <= ocInternalEnd )
                || (!mbJumpCommandReorder && IsOpCodeJumpCommand(eOp)))
        {
            pFacToken = mpToken;
            OpCode eMyLastOp = eOp;
            eOp = NextToken();
            bool bNoParam = false;
            bool bBadName = false;
            if (eOp == ocOpen)
            {
                eOp = NextToken();
                if (eOp == ocClose)
                    bNoParam = true;
                else
                {
                    CheckSetForceArrayParameter( mpToken, 0);
                    eOp = Expression();
                }
            }
            else if (eMyLastOp == ocBad)
            {
                // Just a bad name, not an unknown function, no parameters, no
                // closing expected.
                bBadName = true;
                bNoParam = true;
            }
            else
                SetError( FormulaError::PairExpected);
            sal_uInt32 nSepCount = 0;
            if( !bNoParam )
            {
                bool bDoIICompute = mbComputeII;
                // Array of FormulaToken double pointers to collect the parameters of II opcodes.
                FormulaToken*** pArgArray = nullptr;
                if (bDoIICompute)
                {
                    pArgArray = static_cast<FormulaToken***>(alloca(sizeof(FormulaToken**)*FORMULA_MAXPARAMSII));
                    if (!pArgArray)
                        bDoIICompute = false;
                }

                nSepCount++;

                if (bDoIICompute)
                    pArgArray[nSepCount-1] = pCode - 1; // Add first argument

                while ((eOp == ocSep) && (pArr->GetCodeError() == FormulaError::NONE || !mbStopOnError))
                {
                    NextToken();
                    CheckSetForceArrayParameter( mpToken, nSepCount);
                    nSepCount++;
                    if (nSepCount > FORMULA_MAXPARAMS)
                        SetError( FormulaError::CodeOverflow);
                    eOp = Expression();
                    if (bDoIICompute && nSepCount <= FORMULA_MAXPARAMSII)
                        pArgArray[nSepCount - 1] = pCode - 1; // Add rest of the arguments
                }
                if (bDoIICompute)
                    HandleIIOpCode(pFacToken, pArgArray,
                                   std::min(nSepCount, static_cast<sal_uInt32>(FORMULA_MAXPARAMSII)));
            }
            bool bDone = false;
            if (bBadName)
                ;   // nothing, keep current token for return
            else if (eOp != ocClose)
                SetError( FormulaError::PairExpected);
            else
            {
                NextToken();
                bDone = true;
            }
            // Jumps are just normal functions for the FunctionAutoPilot tree view
            if (!mbJumpCommandReorder && pFacToken->GetType() == svJump)
                pFacToken = new FormulaFAPToken( pFacToken->GetOpCode(), nSepCount, pFacToken );
            else
                pFacToken->SetByte( nSepCount );
            PutCode( pFacToken );

            if (bDone)
                AnnotateOperands();
        }
        else if (IsOpCodeJumpCommand(eOp))
        {
            // the PC counters are -1
            pFacToken = mpToken;
            switch (eOp)
            {
                case ocIf:
                    pFacToken->GetJump()[ 0 ] = 3;  // if, else, behind
                    break;
                case ocChoose:
                    pFacToken->GetJump()[ 0 ] = FORMULA_MAXJUMPCOUNT + 1;
                    break;
                case ocLet:
                    pFacToken->GetJump()[ 0 ] = FORMULA_MAXPARAMS + 1;
                    break;
                case ocIfError:
                case ocIfNA:
                    pFacToken->GetJump()[ 0 ] = 2;  // if, behind
                    break;
                default:
                    SAL_WARN("formula.core","Jump OpCode: " << +eOp);
                    assert(!"FormulaCompiler::Factor: someone forgot to add a jump count case");
            }
            eOp = NextToken();
            if (eOp == ocOpen)
            {
                NextToken();
                CheckSetForceArrayParameter( mpToken, 0);
                eOp = Expression();
            }
            else
                SetError( FormulaError::PairExpected);
            PutCode( pFacToken );
            // During AutoCorrect (since pArr->GetCodeError() is
            // ignored) an unlimited ocIf would crash because
            // ScRawToken::Clone() allocates the JumpBuffer according to
            // nJump[0]*2+2, which is 3*2+2 on ocIf and 2*2+2 ocIfError and ocIfNA.
            short nJumpMax;
            OpCode eFacOpCode = pFacToken->GetOpCode();
            switch (eFacOpCode)
            {
                case ocIf:
                    nJumpMax = 3;
                    break;
                case ocChoose:
                    nJumpMax = FORMULA_MAXJUMPCOUNT;
                    break;
                case ocLet:
                    nJumpMax = FORMULA_MAXPARAMS;
                    break;
                case ocIfError:
                case ocIfNA:
                    nJumpMax = 2;
                    break;
                case ocStop:
                    // May happen only if PutCode(pFacToken) ran into overflow.
                    nJumpMax = 0;
                    assert(pc == FORMULA_MAXTOKENS && pArr->GetCodeError() != FormulaError::NONE);
                    break;
                default:
                    nJumpMax = 0;
                    SAL_WARN("formula.core","Jump OpCode: " << +eFacOpCode);
                    assert(!"FormulaCompiler::Factor: someone forgot to add a jump max case");
            }
            short nJumpCount = 0;
            while ( (nJumpCount < (FORMULA_MAXPARAMS - 1)) && (eOp == ocSep)
                    && (pArr->GetCodeError() == FormulaError::NONE || !mbStopOnError))
            {
                if ( ++nJumpCount <= nJumpMax )
                    pFacToken->GetJump()[nJumpCount] = pc-1;
                NextToken();
                CheckSetForceArrayParameter( mpToken, nJumpCount - 1);
                eOp = Expression();
                // ocSep or ocClose terminate the subexpression
                PutCode( mpToken );
            }
            if (eOp != ocClose)
                SetError( FormulaError::PairExpected);
            else
            {
                NextToken();
                // always limit to nJumpMax, no arbitrary overwrites
                if ( ++nJumpCount <= nJumpMax )
                    pFacToken->GetJump()[ nJumpCount ] = pc-1;
                eFacOpCode = pFacToken->GetOpCode();
                bool bLimitOk;
                switch (eFacOpCode)
                {
                    case ocIf:
                        bLimitOk = (nJumpCount <= 3);
                        break;
                    case ocChoose:
                        bLimitOk = (nJumpCount < FORMULA_MAXJUMPCOUNT);
                        break;
                    case ocLet:
                        bLimitOk = (nJumpCount < FORMULA_MAXPARAMS);
                        break;
                    case ocIfError:
                    case ocIfNA:
                        bLimitOk = (nJumpCount <= 2);
                        break;
                    case ocStop:
                        // May happen only if PutCode(pFacToken) ran into overflow.
                        // This may had resulted from a stacked token array and
                        // error wasn't propagated so assert only the program
                        // counter.
                        bLimitOk = false;
                        assert(pc == FORMULA_MAXTOKENS);
                        break;
                    default:
                        bLimitOk = false;
                        SAL_WARN("formula.core","Jump OpCode: " << +eFacOpCode);
                        assert(!"FormulaCompiler::Factor: someone forgot to add a jump limit case");
                }
                if (bLimitOk)
                    pFacToken->GetJump()[ 0 ] = nJumpCount;
                else
                    SetError( FormulaError::IllegalParameter);
            }
        }
        else if ( eOp == ocMissing )
        {
            PutCode( mpToken );
            NextToken();
        }
        else if ( eOp == ocClose )
        {
            SetError( FormulaError::ParameterExpected );
        }
        else if ( eOp == ocSep )
        {   // Subsequent ocSep
            SetError( FormulaError::ParameterExpected );
            if ( bAutoCorrect && !pStack )
            {
                aCorrectedSymbol.clear();
                bCorrected = true;
            }
        }
        else if ( mpToken->IsExternalRef() )
        {
            PutCode( mpToken);
            NextToken();
        }
        else
        {
            SetError( FormulaError::UnknownToken );
            if ( bAutoCorrect && !pStack )
            {
                if ( eOp == ocStop )
                {   // trailing operator w/o operand
                    sal_Int32 nLen = aCorrectedFormula.getLength();
                    if ( nLen )
                        aCorrectedFormula = aCorrectedFormula.copy( 0, nLen - 1 );
                    aCorrectedSymbol.clear();
                    bCorrected = true;
                }
            }
        }
    }
}

void FormulaCompiler::RangeLine()
{
    Factor();
    while (mpToken->GetOpCode() == ocRange)
    {
        FormulaToken** pCode1 = pCode - 1;
        FormulaTokenRef p = mpToken;
        NextToken();
        Factor();
        FormulaToken** pCode2 = pCode - 1;
        if (!MergeRangeReference( pCode1, pCode2))
            PutCode(p);
    }
}

void FormulaCompiler::IntersectionLine()
{
    RangeLine();
    while (mpToken->GetOpCode() == ocIntersect || mpToken->GetOpCode() == ocSpaces)
    {
        sal_uInt16 nCodeIndex = maArrIterator.GetIndex() - 1;
        FormulaToken** pCode1 = pCode - 1;
        FormulaTokenRef p = mpToken;
        NextToken();
        RangeLine();
        FormulaToken** pCode2 = pCode - 1;
        if (p->GetOpCode() == ocSpaces)
        {
            // Convert to intersection if both left and right are references or
            // functions (potentially returning references, if not then a space
            // or no space would be a syntax error anyway), not other operators
            // or operands. Else discard.
            if (isAdjacentOrGapRpnEnd( pc, pCode, pCode1, pCode2) && isIntersectable( pCode1, pCode2))
            {
                FormulaTokenRef pIntersect( new FormulaByteToken( ocIntersect));
                // Replace ocSpaces with ocIntersect so that when switching
                // formula syntax the correct operator string is created.
                // coverity[freed_arg : FALSE] - FormulaTokenRef has a ref so ReplaceToken won't delete pIntersect
                pArr->ReplaceToken( nCodeIndex, pIntersect.get(), FormulaTokenArray::ReplaceMode::CODE_ONLY);
                PutCode( pIntersect);
            }
        }
        else
        {
            PutCode(p);
        }
    }
}

void FormulaCompiler::UnionLine()
{
    IntersectionLine();
    while (mpToken->GetOpCode() == ocUnion)
    {
        FormulaTokenRef p = mpToken;
        NextToken();
        IntersectionLine();
        PutCode(p);
    }
}

void FormulaCompiler::UnaryLine()
{
    if( mpToken->GetOpCode() == ocAdd )
        GetToken();
    else if (SC_OPCODE_START_UN_OP <= mpToken->GetOpCode() &&
            mpToken->GetOpCode() < SC_OPCODE_STOP_UN_OP)
    {
        FormulaTokenRef p = mpToken;
        NextToken();
        UnaryLine();
        if (mbComputeII)
        {
            FormulaToken** pArg = pCode - 1;
            HandleIIOpCode(p.get(), &pArg, 1);
        }
        PutCode( p );
    }
    else
        UnionLine();
}

void FormulaCompiler::PostOpLine()
{
    UnaryLine();
    while ( mpToken->GetOpCode() == ocPercentSign )
    {   // this operator _follows_ its operand
        if (mbComputeII)
        {
            FormulaToken** pArg = pCode - 1;
            HandleIIOpCode(mpToken.get(), &pArg, 1);
        }
        PutCode( mpToken );
        NextToken();
    }
}

void FormulaCompiler::PowLine()
{
    PostOpLine();
    while (mpToken->GetOpCode() == ocPow)
    {
        FormulaTokenRef p = mpToken;
        FormulaToken** pArgArray[2];
        if (mbComputeII)
            pArgArray[0] = pCode - 1; // Add first argument
        NextToken();
        PostOpLine();
        if (mbComputeII)
        {
            pArgArray[1] = pCode - 1; // Add second argument
            HandleIIOpCode(p.get(), pArgArray, 2);
        }
        PutCode(p);
    }
}

void FormulaCompiler::MulDivLine()
{
    PowLine();
    while (mpToken->GetOpCode() == ocMul || mpToken->GetOpCode() == ocDiv)
    {
        FormulaTokenRef p = mpToken;
        FormulaToken** pArgArray[2];
        if (mbComputeII)
            pArgArray[0] = pCode - 1; // Add first argument
        NextToken();
        PowLine();
        if (mbComputeII)
        {
            pArgArray[1] = pCode - 1; // Add second argument
            HandleIIOpCode(p.get(), pArgArray, 2);
        }
        PutCode(p);
    }
}

void FormulaCompiler::AddSubLine()
{
    MulDivLine();
    while (mpToken->GetOpCode() == ocAdd || mpToken->GetOpCode() == ocSub)
    {
        FormulaTokenRef p = mpToken;
        FormulaToken** pArgArray[2];
        if (mbComputeII)
            pArgArray[0] = pCode - 1; // Add first argument
        NextToken();
        MulDivLine();
        if (mbComputeII)
        {
            pArgArray[1] = pCode - 1; // Add second argument
            HandleIIOpCode(p.get(), pArgArray, 2);
        }
        PutCode(p);
    }
}

void FormulaCompiler::ConcatLine()
{
    AddSubLine();
    while (mpToken->GetOpCode() == ocAmpersand)
    {
        FormulaTokenRef p = mpToken;
        FormulaToken** pArgArray[2];
        if (mbComputeII)
            pArgArray[0] = pCode - 1; // Add first argument
        NextToken();
        AddSubLine();
        if (mbComputeII)
        {
            pArgArray[1] = pCode - 1; // Add second argument
            HandleIIOpCode(p.get(), pArgArray, 2);
        }
        PutCode(p);
    }
}

void FormulaCompiler::CompareLine()
{
    ConcatLine();
    while (mpToken->GetOpCode() >= ocEqual && mpToken->GetOpCode() <= ocGreaterEqual)
    {
        FormulaTokenRef p = mpToken;
        FormulaToken** pArgArray[2];
        if (mbComputeII)
            pArgArray[0] = pCode - 1; // Add first argument
        NextToken();
        ConcatLine();
        if (mbComputeII)
        {
            pArgArray[1] = pCode - 1; // Add second argument
            HandleIIOpCode(p.get(), pArgArray, 2);
        }
        PutCode(p);
    }
}

OpCode FormulaCompiler::Expression()
{
    FormulaCompilerRecursionGuard aRecursionGuard( nRecursion );
    if ( nRecursion > nRecursionMax )
    {
        SetError( FormulaError::StackOverflow );
        return ocStop;      //! generate token instead?
    }
    CompareLine();
    while (mpToken->GetOpCode() == ocAnd || mpToken->GetOpCode() == ocOr)
    {
        FormulaTokenRef p = mpToken;
        mpToken->SetByte( 2 );       // 2 parameters!
        FormulaToken** pArgArray[2];
        if (mbComputeII)
            pArgArray[0] = pCode - 1; // Add first argument
        NextToken();
        CompareLine();
        if (mbComputeII)
        {
            pArgArray[1] = pCode - 1; // Add second argument
            HandleIIOpCode(p.get(), pArgArray, 2);
        }
        PutCode(p);
    }
    return mpToken->GetOpCode();
}


void FormulaCompiler::SetError( FormulaError /*nError*/ )
{
}

FormulaTokenRef FormulaCompiler::ExtendRangeReference( FormulaToken & /*rTok1*/, FormulaToken & /*rTok2*/ )
{
    return FormulaTokenRef();
}

bool FormulaCompiler::MergeRangeReference( FormulaToken * * const pCode1, FormulaToken * const * const pCode2 )
{
    if (!isAdjacentRpnEnd( pc, pCode, pCode1, pCode2))
        return false;

    FormulaToken *p1 = *pCode1, *p2 = *pCode2;
    FormulaTokenRef p = ExtendRangeReference( *p1, *p2);
    if (!p)
        return false;

    p->IncRef();
    p1->DecRef();
    p2->DecRef();
    *pCode1 = p.get();
    --pCode;
    --pc;

    return true;
}

bool FormulaCompiler::CompileTokenArray()
{
    glSubTotal = false;
    bCorrected = false;
    needsRPNTokenCheck = false;
    if (pArr->GetCodeError() == FormulaError::NONE || !mbStopOnError)
    {
        if ( bAutoCorrect )
        {
            aCorrectedFormula.clear();
            aCorrectedSymbol.clear();
        }
        pArr->DelRPN();
        maArrIterator.Reset();
        pStack = nullptr;
        FormulaToken* pDataArray[ FORMULA_MAXTOKENS + 1 ];
        // Code in some places refers to the last token as 'pCode - 1', which may
        // point before the first element if the expression is bad. So insert a dummy
        // node in that place which will make that token be nullptr.
        pDataArray[ 0 ] = nullptr;
        FormulaToken** pData = pDataArray + 1;
        pCode = pData;
        bool bWasForced = pArr->IsRecalcModeForced();
        if ( bWasForced && bAutoCorrect )
            aCorrectedFormula = "=";
        pArr->ClearRecalcMode();
        maArrIterator.Reset();
        eLastOp = ocOpen;
        pc = 0;
        NextToken();
        OpCode eOp = Expression();
        // Some trailing garbage that doesn't form an expression?
        if (eOp != ocStop)
            SetError( FormulaError::OperatorExpected);
        PostProcessCode();

        FormulaError nErrorBeforePop = pArr->GetCodeError();

        while( pStack )
            PopTokenArray();
        if( pc )
        {
            pArr->CreateNewRPNArrayFromData( pData, pc );
            if( needsRPNTokenCheck )
                pArr->CheckAllRPNTokens();
        }

        // once an error, always an error
        if( pArr->GetCodeError() == FormulaError::NONE && nErrorBeforePop != FormulaError::NONE )
            pArr->SetCodeError( nErrorBeforePop);

        if (pArr->GetCodeError() != FormulaError::NONE && mbStopOnError)
        {
            pArr->DelRPN();
            maArrIterator.Reset();
            pArr->SetHyperLink( false);
        }

        if ( bWasForced )
            pArr->SetRecalcModeForced();
    }
    if( nNumFmt == SvNumFormatType::UNDEFINED )
        nNumFmt = SvNumFormatType::NUMBER;
    return glSubTotal;
}

void FormulaCompiler::PopTokenArray()
{
    if( !pStack )
        return;

    FormulaArrayStack* p = pStack;
    pStack = p->pNext;
    // obtain special RecalcMode from SharedFormula
    if ( pArr->IsRecalcModeAlways() )
        p->pArr->SetExclusiveRecalcModeAlways();
    else if ( !pArr->IsRecalcModeNormal() && p->pArr->IsRecalcModeNormal() )
        p->pArr->SetMaskedRecalcMode( pArr->GetRecalcMode() );
    p->pArr->SetCombinedBitsRecalcMode( pArr->GetRecalcMode() );
    if ( pArr->IsHyperLink() )  // fdo 87534
        p->pArr->SetHyperLink( true );
    if( p->bTemp )
        delete pArr;
    pArr = p->pArr;
    maArrIterator = FormulaTokenArrayPlainIterator(*pArr);
    maArrIterator.Jump(p->nIndex);
    mpLastToken = p->mpLastToken;
    delete p;
}

void FormulaCompiler::CreateStringFromTokenArray( OUString& rFormula )
{
    OUStringBuffer aBuffer( pArr->GetLen() * 5 );
    CreateStringFromTokenArray( aBuffer );
    rFormula = aBuffer.makeStringAndClear();
}

void FormulaCompiler::CreateStringFromTokenArray( OUStringBuffer& rBuffer )
{
    rBuffer.setLength(0);
    if( !pArr->GetLen() )
        return;

    FormulaTokenArray* pSaveArr = pArr;
    int nSaveIndex = maArrIterator.GetIndex();
    bool bODFF = FormulaGrammar::isODFF( meGrammar);
    if (bODFF || FormulaGrammar::isPODF( meGrammar) )
    {
        // Scan token array for missing args and re-write if present.
        MissingConventionODF aConv( bODFF);
        if (pArr->NeedsPodfRewrite( aConv))
        {
            pArr = pArr->RewriteMissing( aConv );
            maArrIterator = FormulaTokenArrayPlainIterator( *pArr );
        }
    }
    else if ( FormulaGrammar::isOOXML( meGrammar ) )
    {
        // Scan token array for missing args and rewrite if present.
        if (pArr->NeedsOoxmlRewrite())
        {
            MissingConventionOOXML aConv;
            pArr = pArr->RewriteMissing( aConv );
            maArrIterator = FormulaTokenArrayPlainIterator( *pArr );
        }
    }

    // At least one character per token, plus some are references, some are
    // function names, some are numbers, ...
    rBuffer.ensureCapacity( pArr->GetLen() * 5 );

    if ( pArr->IsRecalcModeForced() )
        rBuffer.append( '=');
    const FormulaToken* t = maArrIterator.First();
    while( t )
        t = CreateStringFromToken( rBuffer, t, true );

    if (pSaveArr != pArr)
    {
        delete pArr;
        pArr = pSaveArr;
        maArrIterator = FormulaTokenArrayPlainIterator( *pArr );
        maArrIterator.Jump(nSaveIndex);
    }
}

const FormulaToken* FormulaCompiler::CreateStringFromToken( OUString& rFormula, const FormulaToken* pTokenP )
{
    OUStringBuffer aBuffer;
    const FormulaToken* p = CreateStringFromToken( aBuffer, pTokenP );
    rFormula += aBuffer;
    return p;
}

const FormulaToken* FormulaCompiler::CreateStringFromToken( OUStringBuffer& rBuffer, const FormulaToken* pTokenP,
        bool bAllowArrAdvance )
{
    bool bNext = true;
    bool bSpaces = false;
    const FormulaToken* t = pTokenP;
    OpCode eOp = t->GetOpCode();
    if( eOp >= ocAnd && eOp <= ocOr )
    {
        // AND, OR infix?
        if ( bAllowArrAdvance )
            t = maArrIterator.Next();
        else
            t = maArrIterator.PeekNext();
        bNext = false;
        bSpaces = ( !t || t->GetOpCode() != ocOpen );
    }
    if( bSpaces )
        rBuffer.append( ' ');

    if (eOp == ocSpaces || eOp == ocWhitespace)
    {
        bool bWriteSpaces = true;
        if (eOp == ocSpaces && mxSymbols->isODFF())
        {
            const FormulaToken* p = maArrIterator.PeekPrevNoSpaces();
            bool bIntersectionOp = (p && p->GetOpCode() == ocColRowName);
            if (bIntersectionOp)
            {
                p = maArrIterator.PeekNextNoSpaces();
                bIntersectionOp = (p && p->GetOpCode() == ocColRowName);
            }
            if (bIntersectionOp)
            {
                rBuffer.append( "!!");
                bWriteSpaces = false;
            }
        }
        if (bWriteSpaces)
        {
            // ODF v1.3 OpenFormula 5.14 Whitespace states "whitespace shall
            // not separate a function name from its initial opening
            // parenthesis".
            //
            // ECMA-376-1:2016 18.17.2 Syntax states "that no space characters
            // shall separate a function-name from the left parenthesis (()
            // that follows it." and Excel even chokes on it.
            //
            // Suppress/remove it in any case also in UI, it will not be
            // preserved.
            const FormulaToken* p = maArrIterator.PeekPrevNoSpaces();
            if (p && p->IsFunction())
            {
                p = maArrIterator.PeekNextNoSpaces();
                if (p && p->GetOpCode() == ocOpen)
                    bWriteSpaces = false;
            }
        }
        if (bWriteSpaces)
        {
            // most times it's just one blank
            sal_uInt8 n = t->GetByte();
            for ( sal_uInt8 j=0; j<n; ++j )
            {
                if (eOp == ocWhitespace)
                    rBuffer.append( t->GetChar());
                else
                    rBuffer.append( ' ');
            }
        }
    }
    else if (eOp == ocTTT)
        rBuffer.append("TTT");
    else if (eOp == ocDebugVar)
        rBuffer.append("__DEBUG_VAR");
    else if (eOp == ocIntersect)
    {
        // Nasty, ugly, horrific, terrifying...
        if (FormulaGrammar::isExcelSyntax( meGrammar))
            rBuffer.append(' ');
        else
            rBuffer.append( mxSymbols->getSymbol( eOp));
    }
    else if ( eOp == ocEasterSunday)
    {
        // EASTERSUNDAY belongs to ODFF since ODF 1.4
        if (m_oODFSavingVersion.has_value()
            && m_oODFSavingVersion.value() >= SvtSaveOptions::ODFSVER_012
            && m_oODFSavingVersion.value() < SvtSaveOptions::ODFSVER_014)
            rBuffer.append(u"ORG.OPENOFFICE." + mxSymbols->getSymbol(eOp));
        else
            rBuffer.append(mxSymbols->getSymbol(eOp));
    }
    else if( static_cast<sal_uInt16>(eOp) < mxSymbols->getSymbolCount())        // Keyword:
        rBuffer.append( mxSymbols->getSymbol( eOp));
    else
    {
        SAL_WARN( "formula.core","unknown OpCode");
        rBuffer.append( GetNativeSymbol( ocErrName ));
    }
    if( bNext )
    {
        if (t->IsExternalRef())
        {
            CreateStringFromExternal( rBuffer, pTokenP);
        }
        else
        {
            switch( t->GetType() )
            {
            case svDouble:
                AppendDouble( rBuffer, t->GetDouble() );
            break;

            case svString:
                if( eOp == ocBad || eOp == ocStringXML || eOp == ocStringName )
                    rBuffer.append( t->GetString().getString());
                else
                    AppendString( rBuffer, t->GetString().getString() );
                break;
            case svSingleRef:
                CreateStringFromSingleRef( rBuffer, t);
                break;
            case svDoubleRef:
                CreateStringFromDoubleRef( rBuffer, t);
                break;
            case svMatrix:
            case svMatrixCell:
                CreateStringFromMatrix( rBuffer, t );
                break;

            case svIndex:
                CreateStringFromIndex( rBuffer, t );
                if (t->GetOpCode() == ocTableRef && bAllowArrAdvance && NeedsTableRefTransformation())
                {
                    // Suppress all TableRef related tokens, the resulting
                    // range was written by CreateStringFromIndex().
                    const FormulaToken* const p = maArrIterator.PeekNext();
                    if (p && p->GetOpCode() == ocTableRefOpen)
                    {
                        int nLevel = 0;
                        do
                        {
                            t = maArrIterator.Next();
                            if (!t)
                                break;

                            // Switch cases correspond with those in
                            // ScCompiler::HandleTableRef()
                            switch (t->GetOpCode())
                            {
                                case ocTableRefOpen:
                                    ++nLevel;
                                    break;
                                case ocTableRefClose:
                                    --nLevel;
                                    break;
                                case ocTableRefItemAll:
                                case ocTableRefItemHeaders:
                                case ocTableRefItemData:
                                case ocTableRefItemTotals:
                                case ocTableRefItemThisRow:
                                case ocSep:
                                case ocPush:
                                case ocRange:
                                case ocSpaces:
                                case ocWhitespace:
                                    break;
                                default:
                                    nLevel = 0;
                                    bNext = false;
                            }
                        } while (nLevel);
                    }
                }
                break;
            case svExternal:
            {
                // mapped or translated name of AddIns
                OUString aAddIn( t->GetExternal() );
                bool bMapped = mxSymbols->isPODF();     // ODF 1.1 directly uses programmatical name
                if (!bMapped && mxSymbols->hasExternals())
                {
                    if (mxSymbols->isOOXML())
                    {
                        // Write compatibility name, if any.
                        if (GetExcelName( aAddIn))
                            bMapped = true;
                    }
                    if (!bMapped)
                    {
                        ExternalHashMap::const_iterator iLook = mxSymbols->getReverseExternalHashMap().find( aAddIn);
                        if (iLook != mxSymbols->getReverseExternalHashMap().end())
                        {
                            aAddIn = (*iLook).second;
                            bMapped = true;
                        }
                    }
                }
                if (!bMapped && !mxSymbols->isEnglish())
                    LocalizeString( aAddIn );
                rBuffer.append( aAddIn);
            }
            break;
            case svError:
                AppendErrorConstant( rBuffer, t->GetError());
            break;
            case svByte:
            case svJump:
            case svFAP:
            case svMissing:
            case svSep:
                break;      // Opcodes
            default:
                SAL_WARN("formula.core", "FormulaCompiler::GetStringFromToken: unknown token type " << t->GetType());
            } // of switch
        }
    }
    if( bSpaces )
        rBuffer.append( ' ');
    if ( bAllowArrAdvance )
    {
        if( bNext )
            t = maArrIterator.Next();
        return t;
    }
    return pTokenP;
}


void FormulaCompiler::AppendDouble( OUStringBuffer& rBuffer, double fVal ) const
{
    if ( mxSymbols->isEnglishLocale() )
    {
        ::rtl::math::doubleToUStringBuffer( rBuffer, fVal,
                rtl_math_StringFormat_Automatic,
                rtl_math_DecimalPlaces_Max, '.', true );
    }
    else
    {
        SvtSysLocale aSysLocale;
        ::rtl::math::doubleToUStringBuffer( rBuffer, fVal,
                rtl_math_StringFormat_Automatic,
                rtl_math_DecimalPlaces_Max,
                aSysLocale.GetLocaleData().getNumDecimalSep()[0],
                true );
    }
}

void FormulaCompiler::AppendBoolean( OUStringBuffer& rBuffer, bool bVal ) const
{
    rBuffer.append( mxSymbols->getSymbol( bVal ? ocTrue : ocFalse ) );
}

void FormulaCompiler::AppendString( OUStringBuffer& rBuffer, const OUString & rStr )
{
    rBuffer.append( '"');
    if ( lcl_UnicodeStrChr( rStr.getStr(), '"' ) == nullptr )
        rBuffer.append( rStr );
    else
    {
        OUString aStr = rStr.replaceAll( "\"", "\"\"" );
        rBuffer.append(aStr);
    }
    rBuffer.append( '"');
}

bool FormulaCompiler::NeedsTableRefTransformation() const
{
    // Currently only UI representations and OOXML export use Table structured
    // references. Not defined in ODFF.
    // Unnecessary to explicitly check for ODFF grammar as the ocTableRefOpen
    // symbol is not defined there.
    return mxSymbols->getSymbol( ocTableRefOpen).isEmpty() || FormulaGrammar::isPODF( meGrammar);
}

void FormulaCompiler::UpdateSeparatorsNative(
    const OUString& rSep, const OUString& rArrayColSep, const OUString& rArrayRowSep )
{
    NonConstOpCodeMapPtr xSymbolsNative;
    lcl_fillNativeSymbols( xSymbolsNative);
    xSymbolsNative->putOpCode( rSep, ocSep, nullptr);
    xSymbolsNative->putOpCode( rArrayColSep, ocArrayColSep, nullptr);
    xSymbolsNative->putOpCode( rArrayRowSep, ocArrayRowSep, nullptr);
}

void FormulaCompiler::ResetNativeSymbols()
{
    NonConstOpCodeMapPtr xSymbolsNative;
    lcl_fillNativeSymbols( xSymbolsNative, InitSymbols::DESTROY);
    lcl_fillNativeSymbols( xSymbolsNative);
}

void FormulaCompiler::SetNativeSymbols( const OpCodeMapPtr& xMap )
{
    NonConstOpCodeMapPtr xSymbolsNative;
    lcl_fillNativeSymbols( xSymbolsNative);
    xSymbolsNative->copyFrom( *xMap );
}


OpCode FormulaCompiler::NextToken()
{
    if( !GetToken() )
        return ocStop;
    OpCode eOp = mpToken->GetOpCode();
    // There must be an operator before a push
    if ( (eOp == ocPush || eOp == ocColRowNameAuto) &&
            !( (eLastOp == ocOpen) || (eLastOp == ocSep) ||
                (SC_OPCODE_START_BIN_OP <= eLastOp && eLastOp < SC_OPCODE_STOP_UN_OP)) )
        SetError( FormulaError::OperatorExpected);
    // Operator and Plus => operator
    if (eOp == ocAdd && (eLastOp == ocOpen || eLastOp == ocSep ||
                (SC_OPCODE_START_BIN_OP <= eLastOp && eLastOp < SC_OPCODE_STOP_UN_OP)))
    {
        FormulaCompilerRecursionGuard aRecursionGuard( nRecursion );
        eOp = NextToken();
    }
    else
    {
        // Before an operator there must not be another operator, with the
        // exception of AND and OR.
        if ( eOp != ocAnd && eOp != ocOr &&
                (SC_OPCODE_START_BIN_OP <= eOp && eOp < SC_OPCODE_STOP_BIN_OP )
                && (eLastOp == ocOpen || eLastOp == ocSep ||
                    (SC_OPCODE_START_BIN_OP <= eLastOp && eLastOp < SC_OPCODE_STOP_UN_OP)))
        {
            SetError( FormulaError::VariableExpected);
            if ( bAutoCorrect && !pStack )
            {
                if ( eOp == eLastOp || eLastOp == ocOpen )
                {   // throw away duplicated operator
                    aCorrectedSymbol.clear();
                    bCorrected = true;
                }
                else
                {
                    sal_Int32 nPos = aCorrectedFormula.getLength();
                    if ( nPos )
                    {
                        nPos--;
                        sal_Unicode c = aCorrectedFormula[ nPos ];
                        switch ( eOp )
                        {   // swap operators
                            case ocGreater:
                                if ( c == mxSymbols->getSymbolChar( ocEqual) )
                                {   // >= instead of =>
                                    aCorrectedFormula = aCorrectedFormula.replaceAt( nPos, 1,
                                        rtl::OUStringChar( mxSymbols->getSymbolChar(ocGreater) ) );
                                    aCorrectedSymbol = OUString(c);
                                    bCorrected = true;
                                }
                            break;
                            case ocLess:
                                if ( c == mxSymbols->getSymbolChar( ocEqual) )
                                {   // <= instead of =<
                                    aCorrectedFormula = aCorrectedFormula.replaceAt( nPos, 1,
                                        rtl::OUStringChar( mxSymbols->getSymbolChar(ocLess) ) );
                                    aCorrectedSymbol = OUString(c);
                                    bCorrected = true;
                                }
                                else if ( c == mxSymbols->getSymbolChar( ocGreater) )
                                {   // <> instead of ><
                                    aCorrectedFormula = aCorrectedFormula.replaceAt( nPos, 1,
                                        rtl::OUStringChar( mxSymbols->getSymbolChar(ocLess) ) );
                                    aCorrectedSymbol = OUString(c);
                                    bCorrected = true;
                                }
                            break;
                            case ocMul:
                                if ( c == mxSymbols->getSymbolChar( ocSub) )
                                {   // *- instead of -*
                                    aCorrectedFormula = aCorrectedFormula.replaceAt( nPos, 1,
                                        rtl::OUStringChar( mxSymbols->getSymbolChar(ocMul) ) );
                                    aCorrectedSymbol = OUString(c);
                                    bCorrected = true;
                                }
                            break;
                            case ocDiv:
                                if ( c == mxSymbols->getSymbolChar( ocSub) )
                                {   // /- instead of -/
                                    aCorrectedFormula = aCorrectedFormula.replaceAt( nPos, 1,
                                        rtl::OUStringChar( mxSymbols->getSymbolChar(ocDiv) ) );
                                    aCorrectedSymbol = OUString(c);
                                    bCorrected = true;
                                }
                            break;
                            default:
                                ;   // nothing
                        }
                    }
                }
            }
        }
        // Nasty, ugly, horrific, terrifying... significant whitespace...
        if (eOp == ocSpaces && FormulaGrammar::isExcelSyntax( meGrammar))
        {
            // Fake an intersection op as last op for the next round, but at
            // least roughly check if it could make sense at all.
            FormulaToken* pPrev = maArrIterator.PeekPrevNoSpaces();
            if (pPrev && isPotentialRangeType( pPrev, false, false))
            {
                FormulaToken* pNext = maArrIterator.PeekNextNoSpaces();
                if (pNext && isPotentialRangeType( pNext, false, true))
                    eLastOp = ocIntersect;
                else
                    eLastOp = eOp;
            }
            else
                eLastOp = eOp;
        }
        else
            eLastOp = eOp;
    }
    return eOp;
}

void FormulaCompiler::PutCode( FormulaTokenRef& p )
{
    if( pc >= FORMULA_MAXTOKENS - 1 )
    {
        if ( pc == FORMULA_MAXTOKENS - 1 )
        {
            SAL_WARN("formula.core", "FormulaCompiler::PutCode - CodeOverflow with OpCode " << +p->GetOpCode());
            p = new FormulaByteToken( ocStop );
            p->IncRef();
            *pCode++ = p.get();
            ++pc;
        }
        SetError( FormulaError::CodeOverflow);
        return;
    }
    if (pArr->GetCodeError() != FormulaError::NONE && mbJumpCommandReorder)
        return;
    ForceArrayOperator( p);
    p->IncRef();
    *pCode++ = p.get();
    pc++;
}


bool FormulaCompiler::HandleExternalReference( const FormulaToken& /*_aToken*/)
{
    return true;
}

bool FormulaCompiler::HandleStringName()
{
    return true;
}

bool FormulaCompiler::HandleRange()
{
    return true;
}

bool FormulaCompiler::HandleColRowName()
{
    return true;
}

bool FormulaCompiler::HandleDbData()
{
    return true;
}

bool FormulaCompiler::HandleTableRef()
{
    return true;
}

void FormulaCompiler::CreateStringFromSingleRef( OUStringBuffer& /*rBuffer*/, const FormulaToken* /*pToken*/) const
{
}

void FormulaCompiler::CreateStringFromDoubleRef( OUStringBuffer& /*rBuffer*/, const FormulaToken* /*pToken*/) const
{
}

void FormulaCompiler::CreateStringFromIndex( OUStringBuffer& /*rBuffer*/, const FormulaToken* /*pToken*/) const
{
}

void FormulaCompiler::CreateStringFromMatrix( OUStringBuffer& /*rBuffer*/, const FormulaToken* /*pToken*/) const
{
}

void FormulaCompiler::CreateStringFromExternal( OUStringBuffer& /*rBuffer*/, const FormulaToken* /*pToken*/) const
{
}

void FormulaCompiler::LocalizeString( OUString& /*rName*/ ) const
{
}

bool FormulaCompiler::GetExcelName( OUString& /*rName*/ ) const
{
    return false;
}

formula::ParamClass FormulaCompiler::GetForceArrayParameter( const FormulaToken* /*pToken*/, sal_uInt16 /*nParam*/ ) const
{
    return ParamClass::Unknown;
}

void FormulaCompiler::ForceArrayOperator( FormulaTokenRef const & rCurr )
{
    if (pCurrentFactorToken.get() == rCurr.get())
        return;

    const OpCode eOp = rCurr->GetOpCode();
    const StackVar eType = rCurr->GetType();
    const bool bInlineArray = (eOp == ocPush && eType == svMatrix);

    if (!bInlineArray)
    {
        if (rCurr->GetInForceArray() != ParamClass::Unknown)
            // Already set, unnecessary to evaluate again. This happens by calls to
            // CurrentFactor::operator=() while descending through Factor() and
            // then ascending back (and down and up, ...),
            // CheckSetForceArrayParameter() and later PutCode().
            return;

        if (!(eOp != ocPush && (eType == svByte || eType == svJump)))
            return;
    }

    // Return class for inline arrays and functions returning array/matrix.
    // It's somewhat unclear what Excel actually does there and in
    // ECMA-376-1:2016 OOXML mentions "call to ... shall be an array formula"
    // only for FREQUENCY() and TRANSPOSE() but not for any other function
    // returning array/matrix or inline arrays, though for the latter has one
    // example in 18.17.2 Syntax:
    // "SUM(SQRT({1,2,3,4})) returns 6.14 when entered normally". However,
    // these need to be treated similar but not as ParamClass::ForceArray
    // (which would contradict the example in
    // https://bugs.documentfoundation.org/show_bug.cgi?id=122301#c19 and A6 of
    // https://bugs.documentfoundation.org/show_bug.cgi?id=133260#c10 ).
    // See also
    // commit d0ded163d8e93dc5b10d7a7c9bdab1d0a6a50bac
    // commit 5413c8871dec08eff19f514f5f391b946a45c86c
    constexpr ParamClass eArrayReturn = ParamClass::ForceArrayReturn;

    if (bInlineArray)
    {
        // rCurr->SetInForceArray() can not be used with ocPush, but ocPush
        // with svMatrix has an implicit ParamClass::ForceArrayReturn.
        if (nCurrentFactorParam > 0 && pCurrentFactorToken
                && pCurrentFactorToken->GetInForceArray() == ParamClass::Unknown
                && GetForceArrayParameter( pCurrentFactorToken.get(), static_cast<sal_uInt16>(nCurrentFactorParam - 1))
                == ParamClass::Value)
        {
            // Propagate to caller as if a function returning an array/matrix
            // was called (see also below).
            pCurrentFactorToken->SetInForceArray( eArrayReturn);
        }
        return;
    }

    if (!pCurrentFactorToken)
    {
        if (mbMatrixFlag)
        {
            // An array/matrix formula acts as ForceArray on all top level
            // operators and function calls, so that can be inherited properly
            // below.
            rCurr->SetInForceArray( ParamClass::ForceArray);
        }
        else if (pc >= 2 && SC_OPCODE_START_BIN_OP <= eOp && eOp < SC_OPCODE_STOP_BIN_OP)
        {
            // Binary operators are not functions followed by arguments
            // and need some peeking into RPN to inspect their operands.
            // Note that array context is not forced if only one
            // of the operands is an array like "={1;2}+A1:A2" returns #VALUE!
            // if entered in column A and not input in array mode, because it
            // involves a range reference with an implicit intersection. Check
            // both arguments are arrays, or the other is ocPush without ranges
            // for "={1;2}+3" or "={1;2}+A1".
            // Note this does not catch "={1;2}+ABS(A1)" that could be forced
            // to array, user still has to close in array mode.
            // The IsMatrixFunction() is only necessary because not all
            // functions returning matrix have ForceArrayReturn (yet?), see
            // OOXML comment above.

            const OpCode eOp1 = pCode[-1]->GetOpCode();
            const OpCode eOp2 = pCode[-2]->GetOpCode();
            const bool b1 = (pCode[-1]->GetInForceArray() != ParamClass::Unknown || IsMatrixFunction(eOp1));
            const bool b2 = (pCode[-2]->GetInForceArray() != ParamClass::Unknown || IsMatrixFunction(eOp2));
            if ((b1 && b2)
                    || (b1 && eOp2 == ocPush && pCode[-2]->GetType() != svDoubleRef)
                    || (b2 && eOp1 == ocPush && pCode[-1]->GetType() != svDoubleRef))
            {
                rCurr->SetInForceArray( eArrayReturn);
            }
        }
        else if (pc >= 1 && SC_OPCODE_START_UN_OP <= eOp && eOp < SC_OPCODE_STOP_UN_OP)
        {
            // Similar for unary operators.
            if (pCode[-1]->GetInForceArray() != ParamClass::Unknown || IsMatrixFunction(pCode[-1]->GetOpCode()))
            {
                rCurr->SetInForceArray( eArrayReturn);
            }
        }
        return;
    }

    // Inherited parameter class.
    const formula::ParamClass eForceType = pCurrentFactorToken->GetInForceArray();
    if (eForceType == ParamClass::ForceArray || eForceType == ParamClass::ReferenceOrRefArray)
    {
        // ReferenceOrRefArray was set only if in ForceArray context already,
        // it is valid for the one function only to indicate the preferred
        // return type. Propagate as ForceArray if not another parameter
        // handling ReferenceOrRefArray.
        if (nCurrentFactorParam > 0
                && (GetForceArrayParameter( pCurrentFactorToken.get(), static_cast<sal_uInt16>(nCurrentFactorParam - 1))
                    == ParamClass::ReferenceOrRefArray))
            rCurr->SetInForceArray( ParamClass::ReferenceOrRefArray);
        else
            rCurr->SetInForceArray( ParamClass::ForceArray);
        return;
    }
    else if (eForceType == ParamClass::ReferenceOrForceArray)
    {
        // Inherit further only if the return class of the nested function is
        // not Reference. Else flag as suppressed.
        if (GetForceArrayParameter( rCurr.get(), SAL_MAX_UINT16) != ParamClass::Reference)
            rCurr->SetInForceArray( eForceType);
        else
            rCurr->SetInForceArray( ParamClass::SuppressedReferenceOrForceArray);
        return;
    }

    if (nCurrentFactorParam <= 0)
        return;

    // Actual current parameter's class.
    const formula::ParamClass eParamType = GetForceArrayParameter(
            pCurrentFactorToken.get(), static_cast<sal_uInt16>(nCurrentFactorParam - 1));
    if (eParamType == ParamClass::ForceArray)
        rCurr->SetInForceArray( eParamType);
    else if (eParamType == ParamClass::ReferenceOrForceArray)
    {
        if (GetForceArrayParameter( rCurr.get(), SAL_MAX_UINT16) != ParamClass::Reference)
            rCurr->SetInForceArray( eParamType);
        else
            rCurr->SetInForceArray( formula::ParamClass::SuppressedReferenceOrForceArray);
    }

    // Propagate a ForceArrayReturn to caller if the called function
    // returns one and the caller so far does not have a stronger array
    // mode set and expects a scalar value for this parameter.
    if (eParamType == ParamClass::Value && pCurrentFactorToken->GetInForceArray() == ParamClass::Unknown)
    {
        if (IsMatrixFunction( eOp))
            pCurrentFactorToken->SetInForceArray( eArrayReturn);
        else if (GetForceArrayParameter( rCurr.get(), SAL_MAX_UINT16) == ParamClass::ForceArrayReturn)
            pCurrentFactorToken->SetInForceArray( ParamClass::ForceArrayReturn);
    }
}

void FormulaCompiler::CheckSetForceArrayParameter( FormulaTokenRef const & rCurr, sal_uInt8 nParam )
{
    if (!pCurrentFactorToken)
        return;

    nCurrentFactorParam = nParam + 1;

    ForceArrayOperator( rCurr);
}

void FormulaCompiler::PushTokenArray( FormulaTokenArray* pa, bool bTemp )
{
    if ( bAutoCorrect && !pStack )
    {   // don't merge stacked subroutine code into entered formula
        aCorrectedFormula += aCorrectedSymbol;
        aCorrectedSymbol.clear();
    }
    FormulaArrayStack* p = new FormulaArrayStack;
    p->pNext      = pStack;
    p->pArr       = pArr;
    p->nIndex     = maArrIterator.GetIndex();
    p->mpLastToken = mpLastToken;
    p->bTemp      = bTemp;
    pStack        = p;
    pArr          = pa;
    maArrIterator = FormulaTokenArrayPlainIterator( *pArr );
}

} // namespace formula

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
