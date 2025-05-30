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

#include <unicode/uscript.h>
#include <i18nlangtag/lang.h>
#include <osl/mutex.hxx>

#include <cppuhelper/factory.hxx>
#include <cppuhelper/supportsservice.hxx>
#include <com/sun/star/linguistic2/ConversionDictionaryType.hpp>
#include <com/sun/star/lang/IllegalArgumentException.hpp>
#include <com/sun/star/i18n/UnicodeScript.hpp>

#include "hhconvdic.hxx"
#include <linguistic/misc.hxx>

using namespace osl;
using namespace com::sun::star;
using namespace com::sun::star::lang;
using namespace com::sun::star::uno;
using namespace com::sun::star::linguistic2;
using namespace linguistic;


constexpr OUString SN_HH_CONV_DICTIONARY = u"com.sun.star.linguistic2.HangulHanjaConversionDictionary"_ustr;



constexpr sal_Int16 SCRIPT_OTHERS = 0;
constexpr sal_Int16 SCRIPT_HANJA  = 1;
constexpr sal_Int16 SCRIPT_HANGUL = 2;

// from i18npool/source/textconversion/textconversion_ko.cxx
/// @throws RuntimeException
static sal_Int16 checkScriptType(sal_Unicode c)
{
  UErrorCode status = U_ZERO_ERROR;

  UScriptCode scriptCode = uscript_getScript(c, &status);

  if ( !U_SUCCESS(status) ) throw RuntimeException();

  return scriptCode == USCRIPT_HANGUL ? SCRIPT_HANGUL :
            scriptCode == USCRIPT_HAN ? SCRIPT_HANJA : SCRIPT_OTHERS;
}


static bool TextIsAllScriptType( std::u16string_view rTxt, sal_Int16 nScriptType )
{
    for (size_t i = 0; i < rTxt.size(); ++i)
    {
        if (checkScriptType( rTxt[i]) != nScriptType)
            return false;
    }
    return true;
}


HHConvDic::HHConvDic( const OUString &rName, const OUString &rMainURL ) :
    ConvDic( rName, LANGUAGE_KOREAN, ConversionDictionaryType::HANGUL_HANJA, true, rMainURL )
{
}


HHConvDic::~HHConvDic()
{
}


void SAL_CALL HHConvDic::addEntry(
        const OUString& aLeftText,
        const OUString& aRightText )
{
    MutexGuard  aGuard( GetLinguMutex() );

    if ((aLeftText.getLength() != aRightText.getLength()) ||
        !TextIsAllScriptType( aLeftText,  SCRIPT_HANGUL ) ||
        !TextIsAllScriptType( aRightText, SCRIPT_HANJA ))
        throw IllegalArgumentException();
    ConvDic::addEntry( aLeftText, aRightText );
}


OUString SAL_CALL HHConvDic::getImplementationName(  )
{
    return u"com.sun.star.lingu2.HHConvDic"_ustr;
}


sal_Bool SAL_CALL HHConvDic::supportsService( const OUString& rServiceName )
{
    return cppu::supportsService(this, rServiceName);
}


uno::Sequence< OUString > SAL_CALL HHConvDic::getSupportedServiceNames(  )
{
    return { SN_CONV_DICTIONARY, SN_HH_CONV_DICTIONARY };
}


/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
