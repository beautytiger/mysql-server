/*
   Copyright (c) 2017, 2020, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have included with MySQL.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/

// Implements the function defined in ndb_dd_sdi.h
#include "storage/ndb/plugin/ndb_dd_sdi.h"

// Using
#include "my_rapidjson_size_t.h"  // IWYU pragma: keep

#include <rapidjson/document.h>      // rapidjson::Document
#include <rapidjson/prettywriter.h>  // rapidjson::PrettyWriter
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>  // rapidjson::Writer

#include "sql/dd/impl/dictionary_impl.h"  // dd::Dictionary_impl::get_target_dd_version
#include "sql/dd/impl/sdi.h"
#include "sql/dd/sdi_fwd.h"
#include "sql/dd/string_type.h"

typedef rapidjson::Writer<dd::RJ_StringBuffer, dd::RJ_Encoding, dd::RJ_Encoding,
                          dd::RJ_Allocator, 0>
    MinifyWriter;
typedef rapidjson::PrettyWriter<dd::RJ_StringBuffer, dd::RJ_Encoding,
                                dd::RJ_Encoding, dd::RJ_Allocator, 0>
    PrettyWriter;

#ifndef DBUG_OFF
/*
  @brief minify a JSON formatted SDI. Remove whitespace and other
  useless data.

  @note the JSON format is normally in 'pretty' format which takes
  up much more storage space and network bandwidth than 'minified' format.

  @sdi the JSON string to minify

  @return minified JSON string or empty string on failure.
*/

static dd::sdi_t minify(dd::sdi_t sdi) {
  dd::RJ_Document doc;
  doc.Parse<0>(sdi.c_str());

  if (doc.HasParseError()) {
    return "";
  }

  dd::RJ_StringBuffer buf;
  MinifyWriter w(buf);
  if (!doc.Accept(w)) {
    return "";
  }

  return buf.GetString();
}
#endif

dd::sdi_t ndb_dd_sdi_prettify(dd::sdi_t sdi) {
  dd::RJ_Document doc;
  doc.Parse<0>(sdi.c_str());

  if (doc.HasParseError()) {
    return "";
  }

  dd::RJ_StringBuffer buf;
  PrettyWriter w(buf);
  if (!doc.Accept(w)) {
    return "";
  }

  return buf.GetString();
}

bool ndb_dd_sdi_deserialize(THD *thd, const dd::sdi_t &sdi, dd::Table *table) {
  // Check the dd_version and sdi_version set in the SDI that has been passed in
  dd::RJ_Document doc;
  doc.Parse<0>(sdi.c_str());
  if (doc.HasParseError()) {
    return true;
  }
  dd::RJ_Value &sdi_version_val = doc["sdi_version"];
  const std::uint64_t sdi_version = sdi_version_val.GetUint64();
  dd::RJ_Value &dd_version_val = doc["dd_version"];
  const unsigned int sdi_dd_version = dd_version_val.GetUint();
  const unsigned int server_dd_version =
      dd::Dictionary_impl::get_target_dd_version();
  if (sdi_version == dd::SDI_VERSION && sdi_dd_version == server_dd_version) {
    // Both SDI and DD versions match, deserialize the SDI as is
    return dd::deserialize(thd, sdi, table);
  }
  if (sdi_version != dd::SDI_VERSION) {
    // The version of the SDI passed in does not match the current version which
    // causes the subsequent deserialization to fail. Workaround the problem by
    // setting the version to the current version.
    sdi_version_val.SetUint64(dd::SDI_VERSION);
  }
  if (sdi_dd_version != server_dd_version) {
    // The dd_version of the SDI passed in does not match the current dd_version
    // which causes the subsequent deserialization to fail. Workaround the
    // problem by setting the version to the current version.
    dd_version_val.SetUint(server_dd_version);
  }
  dd::RJ_StringBuffer buf;
  MinifyWriter w(buf);
  if (!doc.Accept(w)) {
    return true;
  }
  return dd::deserialize(thd, buf.GetString(), table);
}

dd::sdi_t ndb_dd_sdi_serialize(THD *thd, const dd::Table &table,
                               const dd::String_type &schema_name) {
#ifndef DBUG_OFF
  // Verify that dd::serialize generates SDI in minimzed format
  dd::sdi_t sdi = dd::serialize(thd, table, schema_name);
  DBUG_ASSERT(minify(sdi) == sdi);
#endif
  return dd::serialize(thd, table, schema_name);
}
