/**
 * Copyright (C) 2017 MongoDB Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the copyright holders give permission to link the
 * code of portions of this program with the OpenSSL library under certain
 * conditions as described in each individual source file and distribute
 * linked combinations including the program with the OpenSSL library. You
 * must comply with the GNU Affero General Public License in all respects
 * for all of the code used other than as permitted herein. If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version. If you
 * delete this exception statement from all source files in the program,
 * then also delete it in the license file.
 */

#pragma once

#include "mongo/db/matcher/expression.h"
#include "mongo/db/matcher/expression_leaf.h"
#include "mongo/db/matcher/expression_tree.h"

namespace mongo {

class JSONSchemaParser {
public:
    /**
     * Converts a JSON schema, represented as BSON, into a semantically equivalent match expression
     * tree. Returns a non-OK status if the schema is invalid or cannot be parsed.
     */
    static StatusWithMatchExpression parse(BSONObj schema);

private:
    // Parses 'schema' to the semantically equivalent match expression. If the schema has an
    // associated path, e.g. if we are parsing the nested schema for property "myProp" in
    //
    //    {properties: {myProp: <nested-schema>}}
    //
    // then this is passed in 'path'. In this example, the value of 'path' is "myProp". If there is
    // no path, e.g. for top-level schemas, then 'path' is empty.
    static StatusWithMatchExpression _parse(StringData path, BSONObj schema);

    // Parser for the JSON Schema 'properties' keyword.
    static StatusWithMatchExpression _parseProperties(StringData path,
                                                      BSONElement propertiesElt,
                                                      TypeMatchExpression* typeExpr);
};

}  // namespace mongo
