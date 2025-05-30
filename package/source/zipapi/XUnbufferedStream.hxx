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
#ifndef INCLUDED_PACKAGE_SOURCE_ZIPAPI_XUNBUFFEREDSTREAM_HXX
#define INCLUDED_PACKAGE_SOURCE_ZIPAPI_XUNBUFFEREDSTREAM_HXX

#include <optional>

#include <com/sun/star/io/XSeekable.hpp>
#include <com/sun/star/io/XInputStream.hpp>
#include <com/sun/star/xml/crypto/XCipherContext.hpp>

#include <comphelper/refcountedmutex.hxx>
#include <cppuhelper/implbase.hxx>
#include <rtl/ref.hxx>
#include <package/Inflater.hxx>
#include <memory>
#include <ZipEntry.hxx>
#include <CRC32.hxx>

namespace com::sun::star::uno {
    class XComponentContext;
}

#define UNBUFF_STREAM_DATA          0
#define UNBUFF_STREAM_RAW           1
#define UNBUFF_STREAM_WRAPPEDRAW    2

class EncryptionData;
class XUnbufferedStream final : public cppu::WeakImplHelper
<
    css::io::XInputStream
>
{
    rtl::Reference<comphelper::RefCountedMutex> maMutexHolder;

    css::uno::Reference < css::io::XInputStream > mxZipStream;
    css::uno::Reference < css::io::XSeekable > mxZipSeek;
    css::uno::Sequence < sal_Int8 > maCompBuffer, maHeader;
    ZipEntry maEntry;
    sal_Int32 mnBlockSize;
    css::uno::Reference< css::xml::crypto::XCipherContext > m_xCipherContext;
    std::unique_ptr<ZipUtils::Inflater> maInflater;
    bool mbRawStream, mbWrappedRaw;
    sal_Int16 mnHeaderToRead;
    sal_Int64 mnZipCurrent, mnZipEnd, mnZipSize, mnMyCurrent;
    CRC32 maCRC;
    bool mbCheckCRC;

public:
    XUnbufferedStream(
                 const css::uno::Reference< css::uno::XComponentContext >& xContext,
                 rtl::Reference<comphelper::RefCountedMutex> aMutexHolder,
                 ZipEntry const & rEntry,
                 css::uno::Reference < css::io::XInputStream > const & xNewZipStream,
                 const ::rtl::Reference< EncryptionData >& rData,
                 sal_Int8 nStreamMode,
                 ::std::optional<sal_Int64> oDecryptedSize,
                 const OUString& aMediaType,
                 bool bRecoveryMode );

    // allows to read package raw stream
    XUnbufferedStream(
                 rtl::Reference<comphelper::RefCountedMutex> aMutexHolder,
                 const css::uno::Reference < css::io::XInputStream >& xRawStream,
                 const ::rtl::Reference< EncryptionData >& rData );

    sal_Int64 getSize() const { return mnZipSize; }

    virtual ~XUnbufferedStream() override;

    // XInputStream
    virtual sal_Int32 SAL_CALL readBytes( css::uno::Sequence< sal_Int8 >& aData, sal_Int32 nBytesToRead ) override;
    virtual sal_Int32 SAL_CALL readSomeBytes( css::uno::Sequence< sal_Int8 >& aData, sal_Int32 nMaxBytesToRead ) override;
    virtual void SAL_CALL skipBytes( sal_Int32 nBytesToSkip ) override;
    virtual sal_Int32 SAL_CALL available(  ) override;
    virtual void SAL_CALL closeInput(  ) override;
};
#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
