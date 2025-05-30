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
#ifndef INCLUDED_REPORTDESIGN_SOURCE_UI_INC_SECTIONWINDOW_HXX
#define INCLUDED_REPORTDESIGN_SOURCE_UI_INC_SECTIONWINDOW_HXX

#include <com/sun/star/report/XSection.hpp>
#include <vcl/window.hxx>
#include <vcl/split.hxx>
#include <comphelper/propmultiplex.hxx>
#include <cppuhelper/basemutex.hxx>

#include <UndoActions.hxx>
#include "StartMarker.hxx"
#include "EndMarker.hxx"
#include "ReportSection.hxx"

namespace rptui
{
    class OViewsWindow;
    class OSectionWindow :      public vcl::Window
                            ,   public ::cppu::BaseMutex
                            ,   public ::comphelper::OPropertyChangeListener
    {
        VclPtr<OViewsWindow>    m_pParent;
        VclPtr<OStartMarker>    m_aStartMarker;
        VclPtr<OReportSection>  m_aReportSection;
        VclPtr<Splitter>        m_aSplitter;
        VclPtr<OEndMarker>      m_aEndMarker;

        ::rtl::Reference< comphelper::OPropertyChangeMultiplexer> m_pSectionMulti;
        ::rtl::Reference< comphelper::OPropertyChangeMultiplexer> m_pGroupMulti;

        OSectionWindow(OSectionWindow const &) = delete;
        void operator =(OSectionWindow const &) = delete;

        /** set the title of the group header or footer
        *
        * \param _xGroup
        * \param _nResId
        * \param _pGetSection
        * \param _pIsSectionOn
        * @return sal_True when title was set otherwise FALSE
        */
        bool setGroupSectionTitle(
            const css::uno::Reference<css::report::XGroup>& _xGroup, TranslateId pResId,
            const ::std::function<css::uno::Reference<css::report::XSection>(OGroupHelper*)>&
                _pGetSection,
            const ::std::function<bool(OGroupHelper*)>& _pIsSectionOn);

        /** set the title of the (report/page) header or footer
        *
        * \param _xGroup
        * \param _nResId
        * \param _pGetSection
        * \param _pIsSectionOn
        * @return sal_True when title was set otherwise FALSE
        */
        bool setReportSectionTitle(
            const css::uno::Reference<css::report::XReportDefinition>& _xReport, TranslateId pResId,
            const ::std::function<css::uno::Reference<css::report::XSection>(OReportHelper*)>&
                _pGetSection,
            const ::std::function<bool(OReportHelper*)>& _pIsSectionOn);
        void ImplInitSettings();

        DECL_LINK(Collapsed, OColorListener&, void);
        DECL_LINK(StartSplitHdl, Splitter*, void);
        DECL_LINK(SplitHdl, Splitter*, void);
        DECL_LINK(EndSplitHdl, Splitter*, void);


        virtual void DataChanged( const DataChangedEvent& rDCEvt ) override;
        // Window overrides
        virtual void Resize() override;

    protected:
        virtual void    _propertyChanged(const css::beans::PropertyChangeEvent& _rEvent) override;
    public:
        OSectionWindow( OViewsWindow* _pParent
                        ,const css::uno::Reference< css::report::XSection >& _xSection
                        ,const OUString& _sColorEntry);
        virtual ~OSectionWindow() override;
        virtual void dispose() override;

        OStartMarker&    getStartMarker()    { return *m_aStartMarker;     }
        OReportSection&  getReportSection()  { return *m_aReportSection;   }
        OEndMarker&      getEndMarker()      { return *m_aEndMarker;       }
        OViewsWindow*    getViewsWindow()    { return m_pParent;          }

        void    setCollapsed(bool _bCollapsed);

        /** triggers the property browser with the section
            @param  _pStartMarker
        */
        void    showProperties();

        /** set the marker as marked or not marked
            @param  _bMark  set the new state of the marker
        */
        void    setMarked(bool _bMark);

        /** zoom the ruler and view windows
        */
        void zoom(const Fraction& _aZoom);

        void scrollChildren(tools::Long _nThumbPosX);
    };

} // rptui

#endif // INCLUDED_REPORTDESIGN_SOURCE_UI_INC_SECTIONWINDOW_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
