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

#include <sal/config.h>

#include <vector>
#include <map>

#include <com/sun/star/bridge/XBridgeFactory2.hpp>
#include <com/sun/star/lang/XServiceInfo.hpp>
#include <com/sun/star/uno/Reference.hxx>
#include <cppuhelper/basemutex.hxx>
#include <cppuhelper/compbase.hxx>
#include <sal/types.h>

namespace binaryurp {

// That BridgeFactory derives from XComponent appears to be a historic mistake;
// the implementation does not care about a disposed state:

typedef
    cppu::WeakComponentImplHelper<
        css::lang::XServiceInfo,
        css::bridge::XBridgeFactory2 >
    BridgeFactoryBase;

class BridgeFactory : private cppu::BaseMutex, public BridgeFactoryBase
{
public:
    void removeBridge(
        css::uno::Reference< css::bridge::XBridge >
            const & bridge);

    using BridgeFactoryBase::acquire;
    using BridgeFactoryBase::release;

    BridgeFactory(const BridgeFactory&) = delete;
    BridgeFactory& operator=(const BridgeFactory&) = delete;

    BridgeFactory();

    virtual ~BridgeFactory() override;

private:
    virtual OUString SAL_CALL getImplementationName() override;

    virtual sal_Bool SAL_CALL supportsService(OUString const & ServiceName) override;

    virtual css::uno::Sequence< OUString > SAL_CALL
    getSupportedServiceNames() override;

    virtual css::uno::Reference< css::bridge::XBridge >
    SAL_CALL createBridge(
        OUString const & sName, OUString const & sProtocol,
        css::uno::Reference< css::connection::XConnection > const & aConnection,
        css::uno::Reference< css::bridge::XInstanceProvider > const &
                anInstanceProvider) override;

    virtual css::uno::Reference< css::bridge::XBridge >
    SAL_CALL getBridge(
        OUString const & sName) override;

    virtual
    css::uno::Sequence< css::uno::Reference< css::bridge::XBridge > >
    SAL_CALL getExistingBridges() override;

    void SAL_CALL disposing() override;

    typedef
        std::vector< css::uno::Reference< css::bridge::XBridge > >
        BridgeVector;

    typedef
        std::map<
            OUString,
            css::uno::Reference< css::bridge::XBridge > >
        BridgeMap;

    BridgeVector unnamed_;
    BridgeMap named_;
};

}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
