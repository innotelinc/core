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

#include <com/sun/star/beans/PropertyValue.hpp>
#include <com/sun/star/configuration/theDefaultProvider.hpp>
#include <com/sun/star/container/XContainerQuery.hpp>
#include <com/sun/star/container/XNameAccess.hpp>
#include <com/sun/star/embed/VerbDescriptor.hpp>
#include <com/sun/star/document/XTypeDetection.hpp>

#include <osl/diagnose.h>

#include <comphelper/fileformat.h>
#include <comphelper/mimeconfighelper.hxx>
#include <comphelper/classids.hxx>
#include <comphelper/sequenceashashmap.hxx>
#include <comphelper/documentconstants.hxx>
#include <comphelper/propertysequence.hxx>
#include <rtl/ustrbuf.hxx>
#include <o3tl/numeric.hxx>
#include <utility>


using namespace ::com::sun::star;
using namespace comphelper;


MimeConfigurationHelper::MimeConfigurationHelper( uno::Reference< uno::XComponentContext > xContext )
: m_xContext(std::move( xContext ))
{
    if ( !m_xContext.is() )
        throw uno::RuntimeException(u"MimeConfigurationHelper:: empty component context"_ustr);
}


OUString MimeConfigurationHelper::GetStringClassIDRepresentation( const uno::Sequence< sal_Int8 >& aClassID )
{
    OUStringBuffer aResult;

    if ( aClassID.getLength() == 16 )
    {
        for ( sal_Int32 nInd = 0; nInd < aClassID.getLength(); nInd++ )
        {
            if ( nInd == 4 || nInd == 6 || nInd == 8 || nInd == 10 )
                aResult.append("-");

            sal_Int32 nDigit1 = static_cast<sal_Int32>( static_cast<sal_uInt8>(aClassID[nInd]) / 16 );
            sal_Int32 nDigit2 = static_cast<sal_uInt8>(aClassID[nInd]) % 16;
            aResult.append( OUString::number(nDigit1, 16) + OUString::number( nDigit2, 16 ) );
        }
    }

    return aResult.makeStringAndClear();
}

uno::Sequence< sal_Int8 > MimeConfigurationHelper::GetSequenceClassIDRepresentation( std::u16string_view aClassID )
{
    size_t nLength = aClassID.size();
    if ( nLength == 36 )
    {
        OString aCharClassID = OUStringToOString( aClassID, RTL_TEXTENCODING_ASCII_US );
        uno::Sequence< sal_Int8 > aResult( 16 );
        auto pResult = aResult.getArray();

        size_t nStrPointer = 0;
        sal_Int32 nSeqInd = 0;
        while( nSeqInd < 16 && nStrPointer + 1U < nLength )
        {
            sal_uInt8 nDigit1 = o3tl::convertToHex<sal_uInt8>(aCharClassID[nStrPointer++]);
            sal_uInt8 nDigit2 = o3tl::convertToHex<sal_uInt8>(aCharClassID[nStrPointer++]);

            if (nDigit1 > 15 || nDigit2 > 15)
                break;

            pResult[nSeqInd++] = static_cast<sal_Int8>( nDigit1 * 16 + nDigit2 );

            if ( nStrPointer < nLength && aCharClassID[nStrPointer] == '-' )
                nStrPointer++;
        }

        if ( nSeqInd == 16 && nStrPointer == nLength )
            return aResult;
    }

    return uno::Sequence< sal_Int8 >();
}


uno::Reference< container::XNameAccess > MimeConfigurationHelper::GetConfigurationByPathImpl( const OUString& aPath )
{
    uno::Reference< container::XNameAccess > xConfig;

    try
    {
        if ( !m_xConfigProvider.is() )
            m_xConfigProvider = configuration::theDefaultProvider::get( m_xContext );

        uno::Sequence<uno::Any> aArgs(comphelper::InitAnyPropertySequence(
        {
            {"nodepath", uno::Any(aPath)}
        }));
        xConfig.set( m_xConfigProvider->createInstanceWithArguments(
                        u"com.sun.star.configuration.ConfigurationAccess"_ustr,
                        aArgs ),
                     uno::UNO_QUERY );
    }
    catch( uno::Exception& )
    {}

    return xConfig;
}


uno::Reference< container::XNameAccess > MimeConfigurationHelper::GetObjConfiguration()
{
    std::unique_lock aGuard( m_aMutex );

    if ( !m_xObjectConfig.is() )
        m_xObjectConfig = GetConfigurationByPathImpl(
                                         u"/org.openoffice.Office.Embedding/Objects"_ustr );

    return m_xObjectConfig;
}


uno::Reference< container::XNameAccess > MimeConfigurationHelper::GetVerbsConfiguration()
{
    std::unique_lock aGuard( m_aMutex );

    if ( !m_xVerbsConfig.is() )
        m_xVerbsConfig = GetConfigurationByPathImpl(
                                        u"/org.openoffice.Office.Embedding/Verbs"_ustr);

    return m_xVerbsConfig;
}


uno::Reference< container::XNameAccess > MimeConfigurationHelper::GetMediaTypeConfiguration()
{
    std::unique_lock aGuard( m_aMutex );

    if ( !m_xMediaTypeConfig.is() )
        m_xMediaTypeConfig = GetConfigurationByPathImpl(
                    u"/org.openoffice.Office.Embedding/MimeTypeClassIDRelations"_ustr);

    return m_xMediaTypeConfig;
}


uno::Reference< container::XNameAccess > MimeConfigurationHelper::GetFilterFactory()
{
    std::unique_lock aGuard( m_aMutex );

    if ( !m_xFilterFactory.is() )
        m_xFilterFactory.set(
            m_xContext->getServiceManager()->createInstanceWithContext(u"com.sun.star.document.FilterFactory"_ustr, m_xContext),
            uno::UNO_QUERY );

    return m_xFilterFactory;
}


OUString MimeConfigurationHelper::GetDocServiceNameFromFilter( const OUString& aFilterName )
{
    OUString aDocServiceName;

    try
    {
        uno::Reference< container::XNameAccess > xFilterFactory(
            GetFilterFactory(),
            uno::UNO_SET_THROW );

        uno::Any aFilterAnyData = xFilterFactory->getByName( aFilterName );
        uno::Sequence< beans::PropertyValue > aFilterData;
        if ( aFilterAnyData >>= aFilterData )
        {
            for (const auto& prop : aFilterData)
                if ( prop.Name == "DocumentService" )
                    prop.Value >>= aDocServiceName;
        }
    }
    catch( uno::Exception& )
    {}

    return aDocServiceName;
}


OUString MimeConfigurationHelper::GetDocServiceNameFromMediaType( const OUString& aMediaType )
{
    uno::Reference< container::XContainerQuery > xTypeCFG(
            m_xContext->getServiceManager()->createInstanceWithContext(u"com.sun.star.document.TypeDetection"_ustr, m_xContext),
            uno::UNO_QUERY );

    if ( xTypeCFG.is() )
    {
        try
        {
            // make query for all types matching the properties
            uno::Sequence < beans::NamedValue > aSeq { { u"MediaType"_ustr, css::uno::Any(aMediaType) } };

            uno::Reference < container::XEnumeration > xEnum = xTypeCFG->createSubSetEnumerationByProperties( aSeq );
            while ( xEnum->hasMoreElements() )
            {
                uno::Sequence< beans::PropertyValue > aType;
                if ( xEnum->nextElement() >>= aType )
                {
                    for (const auto& prop : aType)
                    {
                        OUString aFilterName;
                        if ( prop.Name == "PreferredFilter"
                          && ( prop.Value >>= aFilterName ) && !aFilterName.isEmpty() )
                        {
                            OUString aDocumentName = GetDocServiceNameFromFilter( aFilterName );
                            if ( !aDocumentName.isEmpty() )
                                return aDocumentName;
                        }
                    }
                }
            }
        }
        catch( uno::Exception& )
        {}
    }

    return OUString();
}


bool MimeConfigurationHelper::GetVerbByShortcut( const OUString& aVerbShortcut,
                                                embed::VerbDescriptor& aDescriptor )
{
    bool bResult = false;

    uno::Reference< container::XNameAccess > xVerbsConfig = GetVerbsConfiguration();
    uno::Reference< container::XNameAccess > xVerbsProps;
    try
    {
        if ( xVerbsConfig.is() && ( xVerbsConfig->getByName( aVerbShortcut ) >>= xVerbsProps ) && xVerbsProps.is() )
        {
            embed::VerbDescriptor aTempDescr;
            static constexpr OUStringLiteral sVerbID = u"VerbID";
            static constexpr OUStringLiteral sVerbUIName = u"VerbUIName";
            static constexpr OUStringLiteral sVerbFlags = u"VerbFlags";
            static constexpr OUStringLiteral sVerbAttributes = u"VerbAttributes";
            if ( ( xVerbsProps->getByName(sVerbID) >>= aTempDescr.VerbID )
              && ( xVerbsProps->getByName(sVerbUIName) >>= aTempDescr.VerbName )
              && ( xVerbsProps->getByName(sVerbFlags) >>= aTempDescr.VerbFlags )
              && ( xVerbsProps->getByName(sVerbAttributes) >>= aTempDescr.VerbAttributes ) )
            {
                aDescriptor = std::move(aTempDescr);
                bResult = true;
            }
        }
    }
    catch( uno::Exception& )
    {
    }

    return bResult;
}


uno::Sequence< beans::NamedValue > MimeConfigurationHelper::GetObjPropsFromConfigEntry(
                                            const uno::Sequence< sal_Int8 >& aClassID,
                                            const uno::Reference< container::XNameAccess >& xObjectProps )
{
    uno::Sequence< beans::NamedValue > aResult;

    if ( aClassID.getLength() == 16 )
    {
        try
        {
            const uno::Sequence< OUString > aObjPropNames = xObjectProps->getElementNames();

            aResult.realloc( aObjPropNames.getLength() + 1 );
            auto pResult = aResult.getArray();
            pResult[0].Name = "ClassID";
            pResult[0].Value <<= aClassID;

            for ( sal_Int32 nInd = 0; nInd < aObjPropNames.getLength(); nInd++ )
            {
                pResult[nInd + 1].Name = aObjPropNames[nInd];

                if ( aObjPropNames[nInd] == "ObjectVerbs" )
                {
                    uno::Sequence< OUString > aVerbShortcuts;
                    if ( !(xObjectProps->getByName( aObjPropNames[nInd] ) >>= aVerbShortcuts) )
                        throw uno::RuntimeException(u"Failed to get verb shortcuts from object properties"_ustr);
                    uno::Sequence< embed::VerbDescriptor > aVerbDescriptors( aVerbShortcuts.getLength() );
                    auto aVerbDescriptorsRange = asNonConstRange(aVerbDescriptors);
                    for ( sal_Int32 nVerbI = 0; nVerbI < aVerbShortcuts.getLength(); nVerbI++ )
                        if ( !GetVerbByShortcut( aVerbShortcuts[nVerbI], aVerbDescriptorsRange[nVerbI] ) )
                            throw uno::RuntimeException(u"Failed to get verb descriptor by shortcut"_ustr);

                    pResult[nInd+1].Value <<= aVerbDescriptors;
                }
                else
                    pResult[nInd+1].Value = xObjectProps->getByName( aObjPropNames[nInd] );
            }
        }
        catch( uno::Exception& )
        {
            aResult.realloc( 0 );
        }
    }

    return aResult;
}


OUString MimeConfigurationHelper::GetExplicitlyRegisteredObjClassID( const OUString& aMediaType )
{
    OUString aStringClassID;

    uno::Reference< container::XNameAccess > xMediaTypeConfig = GetMediaTypeConfiguration();
    try
    {
        if ( xMediaTypeConfig.is() )
            xMediaTypeConfig->getByName( aMediaType ) >>= aStringClassID;
    }
    catch( uno::Exception& )
    {
    }

    return aStringClassID;

}


uno::Sequence< beans::NamedValue > MimeConfigurationHelper::GetObjectPropsByStringClassID(
                                                                const OUString& aStringClassID )
{
    uno::Sequence< beans::NamedValue > aObjProps;

    uno::Sequence< sal_Int8 > aClassID = GetSequenceClassIDRepresentation( aStringClassID );
    if ( ClassIDsEqual( aClassID, GetSequenceClassID( SO3_DUMMY_CLASSID ) ) )
    {
        aObjProps = { { u"ObjectFactory"_ustr,
                        uno::Any(u"com.sun.star.embed.OOoSpecialEmbeddedObjectFactory"_ustr) },
                      { u"ClassID"_ustr, uno::Any(aClassID) } };
        return aObjProps;
    }

    if ( aClassID.getLength() == 16 )
    {
        uno::Reference< container::XNameAccess > xObjConfig = GetObjConfiguration();
        uno::Reference< container::XNameAccess > xObjectProps;
        try
        {
            // TODO/LATER: allow to provide ClassID string in any format, only digits are counted
            if ( xObjConfig.is() && ( xObjConfig->getByName( aStringClassID.toAsciiUpperCase() ) >>= xObjectProps ) && xObjectProps.is() )
                aObjProps = GetObjPropsFromConfigEntry( aClassID, xObjectProps );
        }
        catch( uno::Exception& )
        {
        }
    }

    return aObjProps;
}


uno::Sequence< beans::NamedValue > MimeConfigurationHelper::GetObjectPropsByClassID(
                                                                const uno::Sequence< sal_Int8 >& aClassID )
{
    uno::Sequence< beans::NamedValue > aObjProps;
    if ( ClassIDsEqual( aClassID, GetSequenceClassID( SO3_DUMMY_CLASSID ) ) )
    {
        aObjProps = { { u"ObjectFactory"_ustr,
                        uno::Any(u"com.sun.star.embed.OOoSpecialEmbeddedObjectFactory"_ustr) },
                      { u"ClassID"_ustr, uno::Any(aClassID) } };
    }

    OUString aStringClassID = GetStringClassIDRepresentation( aClassID );
    if ( !aStringClassID.isEmpty() )
    {
        uno::Reference< container::XNameAccess > xObjConfig = GetObjConfiguration();
        uno::Reference< container::XNameAccess > xObjectProps;
        try
        {
            if ( xObjConfig.is() && ( xObjConfig->getByName( aStringClassID.toAsciiUpperCase() ) >>= xObjectProps ) && xObjectProps.is() )
                aObjProps = GetObjPropsFromConfigEntry( aClassID, xObjectProps );
        }
        catch( uno::Exception& )
        {
        }
    }

    return aObjProps;
}


uno::Sequence< beans::NamedValue > MimeConfigurationHelper::GetObjectPropsByMediaType( const OUString& aMediaType )
{
    uno::Sequence< beans::NamedValue > aObject =
                                    GetObjectPropsByStringClassID( GetExplicitlyRegisteredObjClassID( aMediaType ) );
    if ( aObject.hasElements() )
        return aObject;

    OUString aDocumentName = GetDocServiceNameFromMediaType( aMediaType );
    if ( !aDocumentName.isEmpty() )
        return GetObjectPropsByDocumentName( aDocumentName );

    return uno::Sequence< beans::NamedValue >();
}


uno::Sequence< beans::NamedValue > MimeConfigurationHelper::GetObjectPropsByFilter( const OUString& aFilterName )
{
    OUString aDocumentName = GetDocServiceNameFromFilter( aFilterName );
    if ( !aDocumentName.isEmpty() )
        return GetObjectPropsByDocumentName( aDocumentName );

    return uno::Sequence< beans::NamedValue >();
}


uno::Sequence< beans::NamedValue > MimeConfigurationHelper::GetObjectPropsByDocumentName( std::u16string_view aDocName )
{
    if ( !aDocName.empty() )
    {
        uno::Reference< container::XNameAccess > xObjConfig = GetObjConfiguration();
        if ( xObjConfig.is() )
        {
            try
            {
                const uno::Sequence< OUString > aClassIDs = xObjConfig->getElementNames();
                for ( const OUString & id : aClassIDs )
                {
                    uno::Reference< container::XNameAccess > xObjectProps;
                    OUString aEntryDocName;

                    if ( ( xObjConfig->getByName( id ) >>= xObjectProps ) && xObjectProps.is()
                      && ( xObjectProps->getByName(u"ObjectDocumentServiceName"_ustr) >>= aEntryDocName )
                      && aEntryDocName == aDocName )
                    {
                        return GetObjPropsFromConfigEntry( GetSequenceClassIDRepresentation( id ),
                                                            xObjectProps );
                    }
                }
            }
            catch( uno::Exception& )
            {}
        }
    }

    return uno::Sequence< beans::NamedValue >();
}


OUString MimeConfigurationHelper::GetFactoryNameByClassID( const uno::Sequence< sal_Int8 >& aClassID )
{
    return GetFactoryNameByStringClassID( GetStringClassIDRepresentation( aClassID ) );
}


OUString MimeConfigurationHelper::GetFactoryNameByStringClassID( const OUString& aStringClassID )
{
    OUString aResult;

    if ( !aStringClassID.isEmpty() )
    {
        uno::Reference< container::XNameAccess > xObjConfig = GetObjConfiguration();
        uno::Reference< container::XNameAccess > xObjectProps;
        try
        {
            if ( xObjConfig.is() && ( xObjConfig->getByName( aStringClassID.toAsciiUpperCase() ) >>= xObjectProps ) && xObjectProps.is() )
                xObjectProps->getByName(u"ObjectFactory"_ustr) >>= aResult;
        }
        catch( uno::Exception& )
        {
            uno::Sequence< sal_Int8 > aClassID = GetSequenceClassIDRepresentation( aStringClassID );
            if ( ClassIDsEqual( aClassID, GetSequenceClassID( SO3_DUMMY_CLASSID ) ) )
                return u"com.sun.star.embed.OOoSpecialEmbeddedObjectFactory"_ustr;
        }
    }

    return aResult;
}


OUString MimeConfigurationHelper::GetFactoryNameByDocumentName( std::u16string_view aDocName )
{
    OUString aResult;

    if ( !aDocName.empty() )
    {
        uno::Reference< container::XNameAccess > xObjConfig = GetObjConfiguration();
        if ( xObjConfig.is() )
        {
            try
            {
                const uno::Sequence< OUString > aClassIDs = xObjConfig->getElementNames();
                for ( const OUString & id : aClassIDs )
                {
                    uno::Reference< container::XNameAccess > xObjectProps;
                    OUString aEntryDocName;

                    if ( ( xObjConfig->getByName( id ) >>= xObjectProps ) && xObjectProps.is()
                      && ( xObjectProps->getByName( u"ObjectDocumentServiceName"_ustr ) >>= aEntryDocName )
                      && aEntryDocName == aDocName )
                    {
                        xObjectProps->getByName(u"ObjectFactory"_ustr) >>= aResult;
                        break;
                    }
                }
            }
            catch( uno::Exception& )
            {}
        }
    }

    return aResult;
}


OUString MimeConfigurationHelper::GetFactoryNameByMediaType( const OUString& aMediaType )
{
    OUString aResult = GetFactoryNameByStringClassID( GetExplicitlyRegisteredObjClassID( aMediaType ) );

    if ( aResult.isEmpty() )
    {
        OUString aDocumentName = GetDocServiceNameFromMediaType( aMediaType );
        if ( !aDocumentName.isEmpty() )
            aResult = GetFactoryNameByDocumentName( aDocumentName );
    }

    return aResult;
}


OUString MimeConfigurationHelper::UpdateMediaDescriptorWithFilterName(
                                        uno::Sequence< beans::PropertyValue >& aMediaDescr,
                                        bool bIgnoreType )
{
    OUString aFilterName;

    for (const auto& prop : aMediaDescr)
        if ( prop.Name == "FilterName" )
            prop.Value >>= aFilterName;

    if ( aFilterName.isEmpty() )
    {
        // filter name is not specified, so type detection should be done

        uno::Reference< document::XTypeDetection > xTypeDetection(
                m_xContext->getServiceManager()->createInstanceWithContext(u"com.sun.star.document.TypeDetection"_ustr, m_xContext),
                uno::UNO_QUERY_THROW );

        // typedetection can change the mode, add a stream and so on, thus a copy should be used
        uno::Sequence< beans::PropertyValue > aTempMD( aMediaDescr );

        // get TypeName
        OUString aTypeName = xTypeDetection->queryTypeByDescriptor(aTempMD, /*bAllowDeepDetection*/true);

        // get FilterName
        for (const auto& prop : aTempMD)
            if ( prop.Name == "FilterName" )
                prop.Value >>= aFilterName;

        if ( !aFilterName.isEmpty() )
        {
            sal_Int32 nOldLen = aMediaDescr.getLength();
            aMediaDescr.realloc( nOldLen + 1 );
            auto pMediaDescr = aMediaDescr.getArray();
            pMediaDescr[nOldLen].Name = "FilterName";
            pMediaDescr[ nOldLen ].Value <<= aFilterName;

        }
        else if ( !aTypeName.isEmpty() && !bIgnoreType )
        {
            uno::Reference< container::XNameAccess > xNameAccess( xTypeDetection, uno::UNO_QUERY );
            uno::Sequence< beans::PropertyValue > aTypes;

            if ( xNameAccess.is() && ( xNameAccess->getByName( aTypeName ) >>= aTypes ) )
            {
                for (const auto& prop : aTypes)
                {
                    if ( prop.Name == "PreferredFilter" && ( prop.Value >>= aFilterName ) )
                    {
                        sal_Int32 nOldLen = aMediaDescr.getLength();
                        aMediaDescr.realloc( nOldLen + 1 );
                        auto pMediaDescr = aMediaDescr.getArray();
                        pMediaDescr[nOldLen].Name = "FilterName";
                        pMediaDescr[ nOldLen ].Value = prop.Value;
                        break;
                    }
                }
            }
        }
    }

    return aFilterName;
}

OUString MimeConfigurationHelper::UpdateMediaDescriptorWithFilterName(
                        uno::Sequence< beans::PropertyValue >& aMediaDescr,
                        uno::Sequence< beans::NamedValue >& aObject )
{
    OUString aDocName;
    for (const auto& nv : aObject)
        if ( nv.Name == "ObjectDocumentServiceName" )
        {
            nv.Value >>= aDocName;
            break;
        }

    OSL_ENSURE( !aDocName.isEmpty(), "The name must exist at this point!" );


    bool bNeedsAddition = true;
    for ( sal_Int32 nMedInd = 0; nMedInd < aMediaDescr.getLength(); nMedInd++ )
        if ( aMediaDescr[nMedInd].Name == "DocumentService" )
        {
            aMediaDescr.getArray()[nMedInd].Value <<= aDocName;
            bNeedsAddition = false;
            break;
        }

    if ( bNeedsAddition )
    {
        sal_Int32 nOldLen = aMediaDescr.getLength();
        aMediaDescr.realloc( nOldLen + 1 );
        auto pMediaDescr = aMediaDescr.getArray();
        pMediaDescr[nOldLen].Name = "DocumentService";
        pMediaDescr[nOldLen].Value <<= aDocName;
    }

    return UpdateMediaDescriptorWithFilterName( aMediaDescr, true );
}

#ifdef _WIN32

SfxFilterFlags MimeConfigurationHelper::GetFilterFlags( const OUString& aFilterName )
{
    SfxFilterFlags nFlags = SfxFilterFlags::NONE;
    try
    {
        if ( !aFilterName.isEmpty() )
        {
            uno::Reference< container::XNameAccess > xFilterFactory(
                GetFilterFactory(),
                uno::UNO_SET_THROW );

            uno::Any aFilterAny = xFilterFactory->getByName( aFilterName );
            uno::Sequence< beans::PropertyValue > aData;
            if ( aFilterAny >>= aData )
            {
                SequenceAsHashMap aFilterHM( aData );
                nFlags = static_cast<SfxFilterFlags>(aFilterHM.getUnpackedValueOrDefault( "Flags", sal_Int32(0) ));
            }
        }
    } catch( uno::Exception& )
    {}

    return nFlags;
}

bool MimeConfigurationHelper::AddFilterNameCheckOwnFile(
                        uno::Sequence< beans::PropertyValue >& aMediaDescr )
{
    OUString aFilterName = UpdateMediaDescriptorWithFilterName( aMediaDescr, false );
    if ( !aFilterName.isEmpty() )
    {
        SfxFilterFlags nFlags = GetFilterFlags( aFilterName );
        // check the OWN flag
        return bool(nFlags & SfxFilterFlags::OWN);
    }

    return false;
}

#endif

OUString MimeConfigurationHelper::GetDefaultFilterFromServiceName( const OUString& aServiceName, sal_Int32 nVersion )
{
    OUString aResult;

    if ( !aServiceName.isEmpty() && nVersion )
        try
        {
            uno::Reference< container::XContainerQuery > xFilterQuery(
                GetFilterFactory(),
                uno::UNO_QUERY_THROW );

            uno::Sequence< beans::NamedValue > aSearchRequest
            {
                { u"DocumentService"_ustr, css::uno::Any(aServiceName) },
                { u"FileFormatVersion"_ustr, css::uno::Any(nVersion) }
            };

            uno::Reference< container::XEnumeration > xFilterEnum =
                                            xFilterQuery->createSubSetEnumerationByProperties( aSearchRequest );

            // use the first filter that is found
            if ( xFilterEnum.is() )
                while ( xFilterEnum->hasMoreElements() )
                {
                    uno::Sequence< beans::PropertyValue > aProps;
                    if ( xFilterEnum->nextElement() >>= aProps )
                    {
                        SfxFilterFlags nFlags = SfxFilterFlags::NONE;
                        OUString sName;
                        for (const auto & rPropVal : aProps)
                        {
                            if (rPropVal.Name == "Flags")
                            {
                                sal_Int32 nTmp(0);
                                if (rPropVal.Value >>= nTmp)
                                    nFlags = static_cast<SfxFilterFlags>(nTmp);
                            }
                            else if (rPropVal.Name == "Name")
                                rPropVal.Value >>= sName;
                        }

                        // that should be import, export, own filter and not a template filter ( TemplatePath flag )
                        SfxFilterFlags const nRequired = SfxFilterFlags::OWN
                            // fdo#78159 for OOoXML, there is code to convert
                            // to ODF in OCommonEmbeddedObject::store*
                            // so accept it even though there's no export
                            | (SOFFICE_FILEFORMAT_60 == nVersion ? SfxFilterFlags::NONE : SfxFilterFlags::EXPORT)
                            | SfxFilterFlags::IMPORT;
                        if ( ( ( nFlags & nRequired ) == nRequired ) && !( nFlags & SfxFilterFlags::TEMPLATEPATH ) )
                        {
                            // if there are more than one filter the preferred one should be used
                            // if there is no preferred filter the first one will be used
                            if ( aResult.isEmpty() || ( nFlags & SfxFilterFlags::PREFERED ) )
                                aResult = sName;
                            if ( nFlags & SfxFilterFlags::PREFERED )
                                break; // the preferred filter was found
                        }
                    }
                }
        }
        catch( uno::Exception& )
        {}

    return aResult;
}


OUString MimeConfigurationHelper::GetExportFilterFromImportFilter( const OUString& aImportFilterName )
{
    OUString aExportFilterName;

    try
    {
        if ( !aImportFilterName.isEmpty() )
        {
            uno::Reference< container::XNameAccess > xFilterFactory(
                GetFilterFactory(),
                uno::UNO_SET_THROW );

            uno::Any aImpFilterAny = xFilterFactory->getByName( aImportFilterName );
            uno::Sequence< beans::PropertyValue > aImpData;
            if ( aImpFilterAny >>= aImpData )
            {
                SequenceAsHashMap aImpFilterHM( aImpData );
                SfxFilterFlags nFlags = static_cast<SfxFilterFlags>(aImpFilterHM.getUnpackedValueOrDefault( u"Flags"_ustr, sal_Int32(0) ));

                if ( !( nFlags & SfxFilterFlags::IMPORT ) )
                {
                    OSL_FAIL( "This is no import filter!" );
                    throw uno::Exception(u"this is no import filter"_ustr, nullptr);
                }

                if ( nFlags & SfxFilterFlags::EXPORT )
                {
                    aExportFilterName = aImportFilterName;
                }
                else
                {
                    OUString aDocumentServiceName = aImpFilterHM.getUnpackedValueOrDefault( u"DocumentService"_ustr, OUString() );
                    OUString aTypeName = aImpFilterHM.getUnpackedValueOrDefault( u"Type"_ustr, OUString() );

                    OSL_ENSURE( !aDocumentServiceName.isEmpty() && !aTypeName.isEmpty(), "Incomplete filter data!" );
                    if ( !(aDocumentServiceName.isEmpty() || aTypeName.isEmpty()) )
                    {
                        uno::Sequence< beans::NamedValue > aSearchRequest
                        {
                            { u"Type"_ustr, css::uno::Any(aTypeName) },
                            { u"DocumentService"_ustr, css::uno::Any(aDocumentServiceName) }
                        };

                        uno::Sequence< beans::PropertyValue > aExportFilterProps = SearchForFilter(
                            uno::Reference< container::XContainerQuery >( xFilterFactory, uno::UNO_QUERY_THROW ),
                            aSearchRequest,
                            SfxFilterFlags::EXPORT,
                            SfxFilterFlags::INTERNAL );

                        if ( aExportFilterProps.hasElements() )
                        {
                            SequenceAsHashMap aExpPropsHM( aExportFilterProps );
                            aExportFilterName = aExpPropsHM.getUnpackedValueOrDefault( u"Name"_ustr, OUString() );
                        }
                    }
                }
            }
        }
    }
    catch( uno::Exception& )
    {}

    return aExportFilterName;
}


// static
uno::Sequence< beans::PropertyValue > MimeConfigurationHelper::SearchForFilter(
                                                        const uno::Reference< container::XContainerQuery >& xFilterQuery,
                                                        const uno::Sequence< beans::NamedValue >& aSearchRequest,
                                                        SfxFilterFlags nMustFlags,
                                                        SfxFilterFlags nDontFlags )
{
    uno::Sequence< beans::PropertyValue > aFilterProps;
    uno::Reference< container::XEnumeration > xFilterEnum =
                                            xFilterQuery->createSubSetEnumerationByProperties( aSearchRequest );

    // the first default filter will be taken,
    // if there is no filter with flag default the first acceptable filter will be taken
    if ( xFilterEnum.is() )
    {
        while ( xFilterEnum->hasMoreElements() )
        {
            uno::Sequence< beans::PropertyValue > aProps;
            if ( xFilterEnum->nextElement() >>= aProps )
            {
                SequenceAsHashMap aPropsHM( aProps );
                SfxFilterFlags nFlags = static_cast<SfxFilterFlags>(aPropsHM.getUnpackedValueOrDefault(u"Flags"_ustr,
                                                                        sal_Int32(0) ));
                if ( ( ( nFlags & nMustFlags ) == nMustFlags ) && !( nFlags & nDontFlags ) )
                {
                    if ( ( nFlags & SfxFilterFlags::DEFAULT ) == SfxFilterFlags::DEFAULT )
                    {
                        aFilterProps = std::move(aProps);
                        break;
                    }
                    else if ( !aFilterProps.hasElements() )
                        aFilterProps = std::move(aProps);
                }
            }
        }
    }

    return aFilterProps;
}


bool MimeConfigurationHelper::ClassIDsEqual( const uno::Sequence< sal_Int8 >& aClassID1, const uno::Sequence< sal_Int8 >& aClassID2 )
{
    return aClassID1 == aClassID2;
}


uno::Sequence< sal_Int8 > MimeConfigurationHelper::GetSequenceClassID( sal_uInt32 n1, sal_uInt16 n2, sal_uInt16 n3,
                                                sal_uInt8 b8, sal_uInt8 b9, sal_uInt8 b10, sal_uInt8 b11,
                                                sal_uInt8 b12, sal_uInt8 b13, sal_uInt8 b14, sal_uInt8 b15 )
{
    uno::Sequence< sal_Int8 > aResult{ /* [ 0] */ static_cast<sal_Int8>( n1 >> 24 ),
                                       /* [ 1] */ static_cast<sal_Int8>( ( n1 << 8 ) >> 24 ),
                                       /* [ 2] */ static_cast<sal_Int8>( ( n1 << 16 ) >> 24 ),
                                       /* [ 3] */ static_cast<sal_Int8>( ( n1 << 24 ) >> 24 ),
                                       /* [ 4] */ static_cast<sal_Int8>( n2 >> 8 ),
                                       /* [ 5] */ static_cast<sal_Int8>( ( n2 << 8 ) >> 8 ),
                                       /* [ 6] */ static_cast<sal_Int8>( n3 >> 8 ),
                                       /* [ 7] */ static_cast<sal_Int8>( ( n3 << 8 ) >> 8 ),
                                       /* [ 8] */ static_cast<sal_Int8>( b8 ),
                                       /* [ 9] */ static_cast<sal_Int8>( b9 ),
                                       /* [10] */ static_cast<sal_Int8>( b10 ),
                                       /* [11] */ static_cast<sal_Int8>( b11 ),
                                       /* [12] */ static_cast<sal_Int8>( b12 ),
                                       /* [13] */ static_cast<sal_Int8>( b13 ),
                                       /* [14] */ static_cast<sal_Int8>( b14 ),
                                       /* [15] */ static_cast<sal_Int8>( b15 ) };

    return aResult;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
