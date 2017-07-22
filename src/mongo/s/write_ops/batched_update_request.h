/**
 *    Copyright (C) 2013 10gen Inc.
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
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#pragma once

#include <string>
#include <vector>

#include "mongo/base/string_data.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/namespace_string.h"
#include "mongo/s/write_ops/batched_update_document.h"
#include "mongo/util/net/op_msg.h"

namespace mongo {

/**
 * This class represents the layout and content of a batched update runCommand,
 * the request side.
 */
class BatchedUpdateRequest {
    MONGO_DISALLOW_COPYING(BatchedUpdateRequest);

public:
    static const BSONField<std::string> collName;
    static const BSONField<std::vector<BatchedUpdateDocument*>> updates;

    //
    // construction / destruction
    //

    BatchedUpdateRequest();
    ~BatchedUpdateRequest();

    bool isValid(std::string* errMsg) const;
    BSONObj toBSON() const;
    void parseRequest(const OpMsgRequest& request);
    void clear();
    std::string toString() const;

    //
    // individual field accessors
    //

    void setNS(NamespaceString ns);
    const NamespaceString& getNS() const;

    /**
     * updates ownership is transferred to here.
     */
    void addToUpdates(BatchedUpdateDocument* updates);
    void unsetUpdates();
    std::size_t sizeUpdates() const;
    const std::vector<BatchedUpdateDocument*>& getUpdates() const;
    const BatchedUpdateDocument* getUpdatesAt(std::size_t pos) const;

private:
    // Convention: (M)andatory, (O)ptional

    // (M)  collection we're updating from
    NamespaceString _ns;
    bool _isNSSet;

    // (M)  array of individual updates
    std::vector<BatchedUpdateDocument*> _updates;
    bool _isUpdatesSet;
};

}  // namespace mongo
