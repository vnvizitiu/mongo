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

#include "mongo/platform/basic.h"

#include "mongo/db/matcher/schema/json_schema_parser.h"

#include "mongo/bson/bsontypes.h"
#include "mongo/db/matcher/expression_parser.h"
#include "mongo/db/matcher/schema/expression_internal_schema_object_match.h"
#include "mongo/stdx/memory.h"
#include "mongo/util/string_map.h"

namespace mongo {

namespace {
// JSON Schema keyword constants.
constexpr StringData kSchemaMaximumKeyword = "maximum"_sd;
constexpr StringData kSchemaPropertiesKeyword = "properties"_sd;
constexpr StringData kSchemaTypeKeyword = "type"_sd;

/**
 * Constructs and returns a match expression to evaluate a JSON Schema restriction keyword.
 *
 * This handles semantic differences between the MongoDB query language and JSON Schema. MongoDB
 * match expressions which apply to a particular type will reject non-matching types, whereas JSON
 * Schema restriction keywords allow non-matching types. As an example, consider the maxItems
 * keyword. This keyword only applies in JSON Schema if the type is an array, whereas the
 * $_internalSchemaMaxItems match expression node rejects non-arrays.
 *
 * The 'restrictionType' expresses the type to which the JSON Schema restriction applies (e.g.
 * arrays for maxItems). The 'restrictionExpr' is the match expression node which can be used to
 * enforce this restriction, should the types match (e.g. $_internalSchemaMaxItems). 'statedType' is
 * a parsed representation of the JSON Schema type keyword which is in effect.
 */
std::unique_ptr<MatchExpression> makeRestriction(TypeMatchExpression::Type restrictionType,
                                                 std::unique_ptr<MatchExpression> restrictionExpr,
                                                 TypeMatchExpression* statedType) {
    if (statedType) {
        const bool bothNumeric = restrictionType.allNumbers &&
            (statedType->matchesAllNumbers() || isNumericBSONType(statedType->getBSONType()));
        const bool bsonTypesMatch = restrictionType.bsonType == statedType->getBSONType();

        if (bothNumeric || bsonTypesMatch) {
            // This restriction applies only to the type that is already being enforced. We return
            // the restriction unmodified.
            return restrictionExpr;
        } else {
            // This restriction doesn't take any effect, since the type of the schema is different
            // from the type to which this retriction applies.
            //
            // TODO SERVER-30028: Make this use an explicit "always matches" expression.
            return stdx::make_unique<AndMatchExpression>();
        }
    }

    invariant(!statedType);

    auto typeExprForNot = stdx::make_unique<TypeMatchExpression>();
    invariantOK(typeExprForNot->init(restrictionExpr->path(), restrictionType));

    auto notExpr = stdx::make_unique<NotMatchExpression>(typeExprForNot.release());
    auto orExpr = stdx::make_unique<OrMatchExpression>();
    orExpr->add(notExpr.release());
    orExpr->add(restrictionExpr.release());

    return std::move(orExpr);
}

StatusWith<std::unique_ptr<TypeMatchExpression>> parseType(StringData path, BSONElement typeElt) {
    if (!typeElt) {
        return {nullptr};
    }

    if (typeElt.type() != BSONType::String) {
        return {Status(ErrorCodes::TypeMismatch,
                       str::stream() << "$jsonSchema keyword '" << kSchemaTypeKeyword
                                     << "' must be a string")};
    }

    return MatchExpressionParser::parseTypeFromAlias(path, typeElt.valueStringData());
}

StatusWithMatchExpression parseMaximum(StringData path,
                                       BSONElement maximum,
                                       TypeMatchExpression* typeExpr) {
    if (!maximum.isNumber()) {
        return {Status(ErrorCodes::TypeMismatch,
                       str::stream() << "$jsonSchema keyword '" << kSchemaMaximumKeyword
                                     << "' must be a number")};
    }

    if (path.empty()) {
        // This restriction has no affect in a top-level schema, since we only store objects.
        //
        // TODO SERVER-30028: Make this use an explicit "always matches" expression.
        return {stdx::make_unique<AndMatchExpression>()};
    }

    auto lteExpr = stdx::make_unique<LTEMatchExpression>();
    auto status = lteExpr->init(path, maximum);
    if (!status.isOK()) {
        return status;
    }

    // We use Number as a stand-in for all numeric restrictions.
    TypeMatchExpression::Type restrictionType;
    restrictionType.allNumbers = true;
    return makeRestriction(restrictionType, std::move(lteExpr), typeExpr);
}

}  // namespace

StatusWithMatchExpression JSONSchemaParser::_parseProperties(StringData path,
                                                             BSONElement propertiesElt,
                                                             TypeMatchExpression* typeExpr) {
    if (propertiesElt.type() != BSONType::Object) {
        return {Status(ErrorCodes::TypeMismatch,
                       str::stream() << "$jsonSchema keyword '" << kSchemaPropertiesKeyword
                                     << "' must be an object")};
    }
    auto propertiesObj = propertiesElt.embeddedObject();

    auto andExpr = stdx::make_unique<AndMatchExpression>();
    for (auto&& property : propertiesObj) {
        if (property.type() != BSONType::Object) {
            return {ErrorCodes::TypeMismatch,
                    str::stream() << "Nested schema for $jsonSchema property '"
                                  << property.fieldNameStringData()
                                  << "' must be an object"};
        }

        auto nestedSchemaMatch = _parse(property.fieldNameStringData(), property.embeddedObject());
        if (!nestedSchemaMatch.isOK()) {
            return nestedSchemaMatch.getStatus();
        }
        andExpr->add(nestedSchemaMatch.getValue().release());
    }

    // If this is a top-level schema, then we have no path and there is no need for an
    // explicit object match node.
    if (path.empty()) {
        return {std::move(andExpr)};
    }

    auto objectMatch = stdx::make_unique<InternalSchemaObjectMatchExpression>();
    auto objectMatchStatus = objectMatch->init(std::move(andExpr), path);
    if (!objectMatchStatus.isOK()) {
        return objectMatchStatus;
    }

    return makeRestriction(BSONType::Object, std::move(objectMatch), typeExpr);
}

StatusWithMatchExpression JSONSchemaParser::_parse(StringData path, BSONObj schema) {
    // Map from JSON Schema keyword to the corresponding element from 'schema', or to an empty
    // BSONElement if the JSON Schema keyword is not specified.
    StringMap<BSONElement> keywordMap{
        {kSchemaTypeKeyword, {}}, {kSchemaPropertiesKeyword, {}}, {kSchemaMaximumKeyword, {}}};

    for (auto&& elt : schema) {
        auto it = keywordMap.find(elt.fieldNameStringData());
        if (it == keywordMap.end()) {
            return Status(ErrorCodes::FailedToParse,
                          str::stream() << "Unknown $jsonSchema keyword: "
                                        << elt.fieldNameStringData());
        }

        if (it->second) {
            return Status(ErrorCodes::FailedToParse,
                          str::stream() << "Duplicate $jsonSchema keyword: "
                                        << elt.fieldNameStringData());
        }

        keywordMap[elt.fieldNameStringData()] = elt;
    }

    auto typeExpr = parseType(path, keywordMap[kSchemaTypeKeyword]);
    if (!typeExpr.isOK()) {
        return typeExpr.getStatus();
    }

    auto andExpr = stdx::make_unique<AndMatchExpression>();

    if (auto propertiesElt = keywordMap[kSchemaPropertiesKeyword]) {
        auto propertiesExpr = _parseProperties(path, propertiesElt, typeExpr.getValue().get());
        if (!propertiesExpr.isOK()) {
            return propertiesExpr;
        }
        andExpr->add(propertiesExpr.getValue().release());
    }

    if (auto maximumElt = keywordMap[kSchemaMaximumKeyword]) {
        auto maxExpr = parseMaximum(path, maximumElt, typeExpr.getValue().get());
        if (!maxExpr.isOK()) {
            return maxExpr;
        }
        andExpr->add(maxExpr.getValue().release());
    }

    if (path.empty() && typeExpr.getValue() &&
        typeExpr.getValue()->getBSONType() != BSONType::Object) {
        // This is a top-level schema which requires that the type is something other than
        // "object". Since we only know how to store objects, this schema matches nothing.
        return {stdx::make_unique<FalseMatchExpression>(StringData{})};
    }

    if (!path.empty() && typeExpr.getValue()) {
        andExpr->add(typeExpr.getValue().release());
    }
    return {std::move(andExpr)};
}

StatusWithMatchExpression JSONSchemaParser::parse(BSONObj schema) {
    return _parse(StringData{}, schema);
}

}  // namespace mongo
