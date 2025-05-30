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

#ifndef INCLUDED_XMLOFF_CONTROLPROPERTYHDL_HXX
#define INCLUDED_XMLOFF_CONTROLPROPERTYHDL_HXX

#include <sal/config.h>
#include <config_options.h>

#include <memory>

#include <xmloff/dllapi.h>
#include <xmloff/prhdlfac.hxx>
#include <rtl/ustring.hxx>
#include <xmloff/XMLConstantsPropertyHandler.hxx>
#include <xmloff/NamedBoolPropertyHdl.hxx>

namespace xmloff
{


    //= ORotationAngleHandler

    class XMLOFF_DLLPUBLIC ORotationAngleHandler final : public XMLPropertyHandler
    {
    public:
        SAL_DLLPRIVATE ORotationAngleHandler();

        SAL_DLLPRIVATE virtual bool importXML( const OUString& _rStrImpValue, css::uno::Any& _rValue, const SvXMLUnitConverter& _rUnitConverter ) const override;
        SAL_DLLPRIVATE virtual bool exportXML( OUString& _rStrExpValue, const css::uno::Any& _rValue, const SvXMLUnitConverter& _rUnitConverter ) const override;
    };


    //= OFontWidthHandler

    class XMLOFF_DLLPUBLIC OFontWidthHandler final : public XMLPropertyHandler
    {
    public:
        SAL_DLLPRIVATE OFontWidthHandler();

        SAL_DLLPRIVATE virtual bool importXML( const OUString& _rStrImpValue, css::uno::Any& _rValue, const SvXMLUnitConverter& _rUnitConverter ) const override;
        SAL_DLLPRIVATE virtual bool exportXML( OUString& _rStrExpValue, const css::uno::Any& _rValue, const SvXMLUnitConverter& _rUnitConverter ) const override;
    };


    //= OControlBorderHandlerBase

    class XMLOFF_DLLPUBLIC OControlBorderHandler final : public XMLPropertyHandler
    {
    public:
        enum BorderFacet
        {
            STYLE,
            COLOR
        };

        SAL_DLLPRIVATE OControlBorderHandler( const BorderFacet _eFacet );

        SAL_DLLPRIVATE virtual bool importXML( const OUString& _rStrImpValue, css::uno::Any& _rValue, const SvXMLUnitConverter& _rUnitConverter ) const override;
        SAL_DLLPRIVATE virtual bool exportXML( OUString& _rStrExpValue, const css::uno::Any& _rValue, const SvXMLUnitConverter& _rUnitConverter ) const override;

    private:
        BorderFacet m_eFacet;
    };


    //= OControlTextEmphasisHandler

    class OControlTextEmphasisHandler final : public XMLPropertyHandler
    {
    public:
        OControlTextEmphasisHandler();

        virtual bool importXML( const OUString& _rStrImpValue, css::uno::Any& _rValue, const SvXMLUnitConverter& _rUnitConverter ) const override;
        virtual bool exportXML( OUString& _rStrExpValue, const css::uno::Any& _rValue, const SvXMLUnitConverter& _rUnitConverter ) const override;
    };


    //= ImageScaleModeHandler

    class UNLESS_MERGELIBS_MORE(XMLOFF_DLLPUBLIC) ImageScaleModeHandler final : public XMLConstantsPropertyHandler
    {
    public:
        ImageScaleModeHandler();
    };


    //= OControlPropertyHandlerFactory

    class UNLESS_MERGELIBS_MORE(XMLOFF_DLLPUBLIC) OControlPropertyHandlerFactory : public XMLPropertyHandlerFactory
    {
        mutable std::unique_ptr<XMLConstantsPropertyHandler>    m_pTextAlignHandler;
        mutable std::unique_ptr<OControlBorderHandler>          m_pControlBorderStyleHandler;
        mutable std::unique_ptr<OControlBorderHandler>          m_pControlBorderColorHandler;
        mutable std::unique_ptr<ORotationAngleHandler>          m_pRotationAngleHandler;
        mutable std::unique_ptr<OFontWidthHandler>              m_pFontWidthHandler;
        mutable std::unique_ptr<XMLConstantsPropertyHandler>    m_pFontEmphasisHandler;
        mutable std::unique_ptr<XMLConstantsPropertyHandler>    m_pFontReliefHandler;
        mutable std::unique_ptr<XMLNamedBoolPropertyHdl>        m_pTextLineModeHandler;

    public:
        OControlPropertyHandlerFactory();

        virtual const XMLPropertyHandler* GetPropertyHandler(sal_Int32 _nType) const override;
    };


}   // namespace xmloff


#endif // INCLUDED_XMLOFF_CONTROLPROPERTYHDL_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
