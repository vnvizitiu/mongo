/**
 *    Copyright (C) 2017 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include "mongo/platform/basic.h"

#include "mongo/db/logical_session_id.h"

#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/util/assert_util.h"

namespace mongo {

LogicalSessionId::LogicalSessionId() : LogicalSessionId(UUID::gen()) {}

LogicalSessionId::LogicalSessionId(Logical_session_id&& lsid) {
    static_cast<Logical_session_id&>(*this) = lsid;
}

LogicalSessionId::LogicalSessionId(UUID id) {
    setId(std::move(id));
}

LogicalSessionId LogicalSessionId::gen() {
    return {UUID::gen()};
}

StatusWith<LogicalSessionId> LogicalSessionId::parse(const std::string& s) {
    auto res = UUID::parse(s);
    if (!res.isOK()) {
        return res.getStatus();
    }

    return LogicalSessionId{std::move(res.getValue())};
}

LogicalSessionId LogicalSessionId::parse(const BSONObj& doc) {
    IDLParserErrorContext ctx("logical session id");
    LogicalSessionId lsid;
    lsid.parseProtected(ctx, doc);
    return lsid;
}

BSONObj LogicalSessionId::toBSON() const {
    BSONObjBuilder builder;
    serialize(&builder);
    return builder.obj();
}

std::string LogicalSessionId::toString() const {
    return getId().toString();
}

}  // namespace mongo
