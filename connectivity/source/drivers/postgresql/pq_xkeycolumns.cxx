/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*************************************************************************
 *
 *  Effective License of whole file:
 *
 *    This library is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU Lesser General Public
 *    License version 2.1, as published by the Free Software Foundation.
 *
 *    This library is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *    Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public
 *    License along with this library; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 *    MA  02111-1307  USA
 *
 *  Parts "Copyright by Sun Microsystems, Inc" prior to August 2011:
 *
 *    The Contents of this file are made available subject to the terms of
 *    the GNU Lesser General Public License Version 2.1
 *
 *    Copyright: 2000 by Sun Microsystems, Inc.
 *
 *    Contributor(s): Joerg Budischewski
 *
 *  All parts contributed on or after August 2011:
 *
 *    This Source Code Form is subject to the terms of the Mozilla Public
 *    License, v. 2.0. If a copy of the MPL was not distributed with this
 *    file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 ************************************************************************/

#include <sal/log.hxx>
#include <com/sun/star/lang/WrappedTargetRuntimeException.hpp>
#include <com/sun/star/sdbc/SQLException.hpp>
#include <com/sun/star/sdbc/XRow.hpp>
#include <cppuhelper/exc_hlp.hxx>
#include <rtl/ref.hxx>
#include <utility>

#include "pq_xcolumns.hxx"
#include "pq_xkeycolumns.hxx"
#include "pq_xkeycolumn.hxx"
#include "pq_statics.hxx"
#include "pq_tools.hxx"

using osl::MutexGuard;

using com::sun::star::beans::XPropertySet;

using com::sun::star::uno::Any;
using com::sun::star::uno::UNO_QUERY;
using com::sun::star::uno::Reference;
using com::sun::star::uno::Sequence;

using com::sun::star::sdbc::XRow;
using com::sun::star::sdbc::XResultSet;
using com::sun::star::sdbc::XDatabaseMetaData;
using com::sun::star::sdbc::SQLException;

namespace pq_sdbc_driver
{

KeyColumns::KeyColumns(
        const ::rtl::Reference< comphelper::RefCountedMutex > & refMutex,
        const css::uno::Reference< css::sdbc::XConnection >  & origin,
        ConnectionSettings *pSettings,
        OUString schemaName,
        OUString tableName,
        const Sequence< OUString > &columnNames,
        const Sequence< OUString > &foreignColumnNames )
    : Container( refMutex, origin, pSettings,  u"KEY_COLUMN"_ustr ),
      m_schemaName(std::move( schemaName )),
      m_tableName(std::move( tableName )),
      m_columnNames( columnNames ),
      m_foreignColumnNames( foreignColumnNames )
{}

KeyColumns::~KeyColumns()
{}


void KeyColumns::refresh()
{
    try
    {
        SAL_INFO("connectivity.postgresql", "sdbcx.KeyColumns get refreshed for table " << m_schemaName << "." << m_tableName);

        osl::MutexGuard guard( m_xMutex->GetMutex() );

        Statics &st = getStatics();
        Reference< XDatabaseMetaData > meta = m_origin->getMetaData();

        Reference< XResultSet > rs =
            meta->getColumns( Any(), m_schemaName, m_tableName, st.cPERCENT );

        DisposeGuard disposeIt( rs );
        Reference< XRow > xRow( rs , UNO_QUERY );

        String2IntMap map;

        m_values.clear();
        sal_Int32 columnIndex = 0;
        while( rs->next() )
        {
            OUString columnName = xRow->getString( 4 );

            int keyindex;
            for( keyindex = 0 ; keyindex < m_columnNames.getLength() ; keyindex ++ )
            {
                if( columnName == m_columnNames[keyindex] )
                    break;
            }
            if( m_columnNames.getLength() == keyindex )
                continue;

            rtl::Reference<KeyColumn> pKeyColumn =
                new KeyColumn( m_xMutex, m_origin, m_pSettings );

            OUString name = columnMetaData2SDBCX( pKeyColumn.get(), xRow );
            if( keyindex < m_foreignColumnNames.getLength() )
            {
                pKeyColumn->setPropertyValue_NoBroadcast_public(
                    st.RELATED_COLUMN, Any( m_foreignColumnNames[keyindex]) );
            }

            {
                m_values.emplace_back(Reference< css::beans::XPropertySet >(pKeyColumn));
                map[ name ] = columnIndex;
                ++columnIndex;
            }
        }
        m_name2index.swap( map );
    }
    catch ( css::sdbc::SQLException & e )
    {
        css::uno::Any anyEx = cppu::getCaughtException();
        throw css::lang::WrappedTargetRuntimeException( e.Message,
                        e.Context, anyEx );
    }

    fire( RefreshedBroadcaster( *this ) );
}


void KeyColumns::appendByDescriptor(
    const css::uno::Reference< css::beans::XPropertySet >& )
{
    throw css::sdbc::SQLException(
        u"KeyColumns::appendByDescriptor not implemented yet"_ustr,
        *this, OUString(), 1, Any() );

//     osl::MutexGuard guard( m_xMutex->GetMutex() );
//     Statics & st = getStatics();
//     Reference< XPropertySet > past = createDataDescriptor();
//     past->setPropertyValue( st.IS_NULLABLE, makeAny( css::sdbc::ColumnValue::NULLABLE ) );
//     alterColumnByDescriptor(
//         m_schemaName, m_tableName, m_pSettings->encoding, m_origin->createStatement() , past, future  );

}


void KeyColumns::dropByIndex( sal_Int32 )
{
    throw css::sdbc::SQLException(
        u"KeyColumns::dropByIndex not implemented yet"_ustr,
        *this, OUString(), 1, Any() );
//     osl::MutexGuard guard( m_xMutex->GetMutex() );
//     if( index < 0 ||  index >= m_values.getLength() )
//     {
//         OUStringBuffer buf( 128 );
//         buf.appendAscii( "COLUMNS: Index out of range (allowed 0 to " );
//         buf.append((sal_Int32)(m_values.getLength() -1) );
//         buf.appendAscii( ", got " );
//         buf.append( index );
//         buf.appendAscii( ")" );
//         throw css::lang::IndexOutOfBoundsException(
//             buf.makeStringAndClear(), *this );
//     }

//     Reference< XPropertySet > set;
//     m_values[index] >>= set;
//     Statics &st = getStatics();
//     OUString name;
//     set->getPropertyValue( st.NAME ) >>= name;

//     OUStringBuffer update( 128 );
//     update.appendAscii( "ALTER TABLE ONLY");
//     bufferQuoteQualifiedIdentifier( update, m_schemaName, m_tableName );
//     update.appendAscii( "DROP COLUMN" );
//     bufferQuoteIdentifier( update, name );
//     Reference< XStatement > stmt = m_origin->createStatement( );
//     DisposeGuard disposeIt( stmt );
//     stmt->executeUpdate( update.makeStringAndClear() );

}


Reference< css::beans::XPropertySet > KeyColumns::createDataDescriptor()
{
    return new KeyColumnDescriptor( m_xMutex, m_origin, m_pSettings );
}

Reference< css::container::XNameAccess > KeyColumns::create(
    const ::rtl::Reference< comphelper::RefCountedMutex > & refMutex,
    const css::uno::Reference< css::sdbc::XConnection >  & origin,
    ConnectionSettings *pSettings,
    const OUString &schemaName,
    const OUString &tableName,
    const Sequence< OUString > &columnNames ,
    const Sequence< OUString > &foreignColumnNames )
{
    rtl::Reference<KeyColumns> pKeyColumns = new KeyColumns(
        refMutex, origin, pSettings, schemaName, tableName, columnNames, foreignColumnNames );
    pKeyColumns->refresh();

    return pKeyColumns;
}


KeyColumnDescriptors::KeyColumnDescriptors(
        const ::rtl::Reference< comphelper::RefCountedMutex > & refMutex,
        const css::uno::Reference< css::sdbc::XConnection >  & origin,
        ConnectionSettings *pSettings )
    : Container( refMutex, origin, pSettings,  u"KEY_COLUMN"_ustr )
{}

Reference< css::beans::XPropertySet > KeyColumnDescriptors::createDataDescriptor()
{
    return new KeyColumnDescriptor( m_xMutex, m_origin, m_pSettings );
}
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
