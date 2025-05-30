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

#include <tools/ref.hxx>
#include <vcl/dllapi.h>
#include <vector>

class ImplFontCharMap;
typedef tools::SvRef<ImplFontCharMap> ImplFontCharMapRef;

class ImplFontCharMap final : public SvRefBase
{
public:
    explicit            ImplFontCharMap(bool bMicrosoftSymbolMap,
                                        std::vector<sal_uInt32> aRangeCodes);
    virtual             ~ImplFontCharMap() override;

private:
    friend class FontCharMap;

                        ImplFontCharMap( const ImplFontCharMap& ) = delete;
    void                operator=( const ImplFontCharMap& ) = delete;

    static ImplFontCharMapRef const & getDefaultMap(bool bMicrosoftSymbolMap = false);
    bool                isDefaultMap() const;

private:
    std::vector<sal_uInt32> maRangeCodes; // pairs of StartCode/(EndCode+1)
    int                 mnCharCount;      // covered codepoints
    const bool m_bMicrosoftSymbolMap;
};

bool VCL_DLLPUBLIC HasMicrosoftSymbolCmap(const unsigned char* pRawData, int nRawLength);

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
