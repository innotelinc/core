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

#include <vcl/filter/ImportOutput.hxx>
#include <vcl/bitmap.hxx>

#include <vcl/BitmapWriteAccess.hxx>

enum class GraphicFilterImportFlags;

enum ReadState
{
    JPEGREAD_OK,
    JPEGREAD_ERROR
};

struct JPEGCreateBitmapParam
{
    tools::ULong nWidth;
    tools::ULong nHeight;
    tools::ULong density_unit;
    tools::ULong X_density;
    tools::ULong Y_density;

    bool bGray;
};

class JPEGReader
{
    SvStream& mrStream;
    std::optional<Bitmap> mpBitmap;
    tools::Long mnLastPos;
    bool mbSetLogSize;

public:
    JPEGReader( SvStream& rStream, GraphicFilterImportFlags nImportFlags );

    ReadState Read(ImportOutput& rImportOutput, GraphicFilterImportFlags nImportFlags, BitmapScopedWriteAccess* ppAccess);

    bool CreateBitmap(JPEGCreateBitmapParam const & param);

    Bitmap& GetBitmap() { return *mpBitmap; }
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
