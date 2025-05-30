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
#pragma once

#include "OPropertySet.hxx"
#include <cppuhelper/implbase.hxx>
#include <comphelper/uno3.hxx>
#include <com/sun/star/lang/XServiceInfo.hpp>
#include <com/sun/star/chart2/XDataPointCustomLabelField.hpp>
#include <com/sun/star/util/XCloneable.hpp>
#include "ModifyListenerHelper.hxx"
#include "PropertyHelper.hxx"

namespace chart
{

namespace impl
{
typedef ::cppu::WeakImplHelper<
    css::chart2::XDataPointCustomLabelField, // inherits from XFormattedString2
    css::lang::XServiceInfo,
    css::util::XCloneable,
    css::util::XModifyBroadcaster,
    css::util::XModifyListener >
    FormattedString_Base;
}

class FormattedString final :
    public impl::FormattedString_Base,
    public ::property::OPropertySet
{
public:
    explicit FormattedString();
    virtual ~FormattedString() override;

    /// declare XServiceInfo methods
    virtual OUString SAL_CALL getImplementationName() override;
    virtual sal_Bool SAL_CALL supportsService( const OUString& ServiceName ) override;
    virtual css::uno::Sequence< OUString > SAL_CALL getSupportedServiceNames() override;

    /// merge XInterface implementations
    DECLARE_XINTERFACE()
    /// merge XTypeProvider implementations
    DECLARE_XTYPEPROVIDER()

    virtual void SAL_CALL setPropertyValue(const OUString& p1, const css::uno::Any& p2) override
        { ::property::OPropertySet::setPropertyValue(p1, p2); }
    virtual css::uno::Any SAL_CALL getPropertyValue(const OUString& p1) override
        { return ::property::OPropertySet::getPropertyValue(p1); }
    virtual void SAL_CALL addPropertyChangeListener(const OUString& p1, const css::uno::Reference<css::beans::XPropertyChangeListener>& p2) override
        { ::property::OPropertySet::addPropertyChangeListener(p1, p2); }
    virtual void SAL_CALL removePropertyChangeListener(const OUString& p1, const css::uno::Reference<css::beans::XPropertyChangeListener>& p2) override
        { ::property::OPropertySet::removePropertyChangeListener(p1, p2); }
    virtual void SAL_CALL addVetoableChangeListener(const OUString& p1, const css::uno::Reference<css::beans::XVetoableChangeListener>& p2) override
        { ::property::OPropertySet::addVetoableChangeListener(p1, p2); }
    virtual void SAL_CALL removeVetoableChangeListener(const OUString& p1, const css::uno::Reference<css::beans::XVetoableChangeListener>& p2) override
        { ::property::OPropertySet::removeVetoableChangeListener(p1, p2); }

    explicit FormattedString( const FormattedString & rOther );

    // ____ XFormattedString ____
    virtual OUString SAL_CALL getString() override;
    virtual void SAL_CALL setString( const OUString& String ) override;

    // ____ XDataPointCustomLabelField ____
    virtual css::chart2::DataPointCustomLabelFieldType SAL_CALL getFieldType() override;
    virtual void SAL_CALL
        setFieldType( const css::chart2::DataPointCustomLabelFieldType FieldType ) override;
    virtual OUString SAL_CALL getGuid() override;
    void SAL_CALL setGuid( const OUString& guid ) override;
    virtual sal_Bool SAL_CALL getDataLabelsRange() override;
    virtual void SAL_CALL setDataLabelsRange( sal_Bool dataLabelsRange ) override;
    virtual OUString SAL_CALL getCellRange() override;
    virtual void SAL_CALL setCellRange( const OUString& cellRange ) override;

    // ____ OPropertySet ____
    virtual void GetDefaultValue( sal_Int32 nHandle, css::uno::Any& rAny ) const override;

    // ____ OPropertySet ____
    virtual ::cppu::IPropertyArrayHelper & SAL_CALL getInfoHelper() override;

    // ____ XPropertySet ____
    virtual css::uno::Reference< css::beans::XPropertySetInfo > SAL_CALL
        getPropertySetInfo() override;

    // ____ XCloneable ____
    virtual css::uno::Reference< css::util::XCloneable > SAL_CALL createClone() override;

    // ____ XModifyBroadcaster ____
    virtual void SAL_CALL addModifyListener(
        const css::uno::Reference< css::util::XModifyListener >& aListener ) override;
    virtual void SAL_CALL removeModifyListener(
        const css::uno::Reference< css::util::XModifyListener >& aListener ) override;

    // ____ XModifyListener ____
    virtual void SAL_CALL modified(
        const css::lang::EventObject& aEvent ) override;

    // ____ XEventListener (base of XModifyListener) ____
    virtual void SAL_CALL disposing(
        const css::lang::EventObject& Source ) override;

private:
    // ____ OPropertySet ____
    virtual void firePropertyChangeEvent() override;
    using OPropertySet::disposing;

    void fireModifyEvent();

    // ____ XFormattedString ____
    OUString m_aString;

    // ____ XDataPointCustomLabelField ____
    css::chart2::DataPointCustomLabelFieldType m_aType;
    OUString m_aGuid;
    OUString m_aCellRange;
    bool m_bDataLabelsRange;

    rtl::Reference<ModifyEventForwarder> m_xModifyEventForwarder;
};

const ::chart::tPropertyValueMap & StaticFormattedStringDefaults();

} //  namespace chart

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
