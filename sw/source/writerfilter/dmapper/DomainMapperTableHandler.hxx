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

#include "PropertyMap.hxx"
#include <vector>

#include <com/sun/star/text/XTextAppendAndConvert.hpp>

namespace writerfilter::dmapper {

typedef css::uno::Sequence< css::uno::Reference< css::text::XTextRange > > CellSequence_t;
typedef css::uno::Sequence<CellSequence_t> RowSequence_t;

typedef css::uno::Sequence< css::uno::Sequence<css::beans::PropertyValues> >  CellPropertyValuesSeq_t;

typedef std::vector<PropertyMapPtr>     PropertyMapVector1;
typedef std::vector<PropertyMapVector1> PropertyMapVector2;

class DomainMapper_Impl;
class TableStyleSheetEntry;
struct TableInfo;

/// A horizontally merged cell is in fact a range of cells till its merge is performed.
struct HorizontallyMergedCell
{
    sal_Int32 m_nFirstRow;
    sal_Int32 m_nFirstCol;
    sal_Int32 m_nLastRow;
    sal_Int32 m_nLastCol;
    HorizontallyMergedCell(sal_Int32 nFirstRow, sal_Int32 nFirstCol)
        : m_nFirstRow(nFirstRow)
        , m_nFirstCol(nFirstCol)
        , m_nLastRow(nFirstRow)
        , m_nLastCol(-1)
    {
    }
};

/// Class to handle events generated by TableManager::resolveCurrentTable().
class DomainMapperTableHandler final : public virtual SvRefBase
{
    rtl::Reference<SwXText>  m_xText;
    DomainMapper_Impl&      m_rDMapper_Impl;
    std::vector< css::uno::Reference<css::text::XTextRange> > m_aCellRange;
    std::vector<CellSequence_t> m_aRowRanges;
    std::vector<RowSequence_t> m_aTableRanges;

    // properties
    PropertyMapVector2      m_aCellProperties;
    PropertyMapVector1      m_aRowProperties;
    TablePropertyMapPtr     m_aTableProperties;

    TableStyleSheetEntry * endTableGetTableStyle(TableInfo & rInfo,
                    std::vector<css::beans::PropertyValue>& rFrameProperties,
                    bool bConvertToFloating);
    CellPropertyValuesSeq_t endTableGetCellProperties(TableInfo & rInfo, std::vector<HorizontallyMergedCell>& rMerges);
    css::uno::Sequence<css::beans::PropertyValues> endTableGetRowProperties();

public:
    typedef tools::SvRef<DomainMapperTableHandler> Pointer_t;

    DomainMapperTableHandler(rtl::Reference<SwXText> xText,
                             DomainMapper_Impl& rDMapper_Impl);
    ~DomainMapperTableHandler() override;

    /**
       Handle start of table.

       @param pProps  properties of the table
     */
    void startTable(const TablePropertyMapPtr& pProps);

    void ApplyParagraphPropertiesFromTableStyle(TableParagraph rParaProp, std::vector< PropertyIds > aAllTableProperties, const css::beans::PropertyValues & rCellProperties);

    /// Handle end of table.
    void endTable(unsigned int nestedTableLevel);
    /**
       Handle start of row.

       @param pProps   properties of the row
     */
    void startRow(const TablePropertyMapPtr& pProps);
    /// Handle end of row.
    void endRow();
    /**
       Handle start of cell.

       @param rT     start handle of the cell
       @param pProps properties of the cell
    */
    void startCell(const css::uno::Reference< css::text::XTextRange > & start, const TablePropertyMapPtr& pProps);
    /**
        Handle end of cell.

        @param rT    end handle of cell
    */
    void endCell(const css::uno::Reference< css::text::XTextRange > & end);

    DomainMapper_Impl& getDomainMapperImpl();
};

}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
