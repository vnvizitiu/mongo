// mongo.js

// NOTE 'Mongo' may be defined here or in MongoJS.cpp.  Add code to init, not to this constructor.
if (typeof Mongo == "undefined") {
    Mongo = function(host) {
        this.init(host);
    };
}

if (!Mongo.prototype) {
    throw Error("Mongo.prototype not defined");
}

(function(original) {
    Mongo.prototype.find = function find(ns, query, fields, limit, skip, batchSize, options) {
        const self = this;
        if (this._isCausal) {
            query = this._gossipLogicalTime(query);
        }
        const res = original.call(this, ns, query, fields, limit, skip, batchSize, options);
        const origNext = res.next;
        res.next = function next() {
            const ret = origNext.call(this);
            self._setLogicalTimeFromReply(ret);
            return ret;
        };
        return res;
    };
})(Mongo.prototype.find);

if (!Mongo.prototype.insert)
    Mongo.prototype.insert = function(ns, obj) {
        throw Error("insert not implemented");
    };
if (!Mongo.prototype.remove)
    Mongo.prototype.remove = function(ns, pattern) {
        throw Error("remove not implemented");
    };
if (!Mongo.prototype.update)
    Mongo.prototype.update = function(ns, query, obj, upsert) {
        throw Error("update not implemented");
    };

if (typeof mongoInject == "function") {
    mongoInject(Mongo.prototype);
}

Mongo.prototype.setCausalConsistency = function(value) {
    if (arguments.length === 0) {
        value = true;
    }
    this._isCausal = value;
};

Mongo.prototype.isCausalConsistencyEnabled = function(cmdObj) {
    var cmdName = (() => {
        for (var name in cmdObj) {
            return name;
        }
        doassert("empty cmdObj");
    })();

    if (!this._isCausal) {
        return false;
    }

    // Currently, read concern afterClusterTime is only supported for commands that support read
    // concern level majority.
    var commandsThatSupportMajorityReadConcern = [
        "count",
        "distinct",
        "find",
        "geoNear",
        "geoSearch",
        "group",
        "mapReduce",
        "mapreduce",
        "parallelCollectionScan",
    ];

    var supportsMajorityReadConcern =
        Array.contains(commandsThatSupportMajorityReadConcern, cmdName);

    if (cmdName === "aggregate") {
        // Aggregate can be either a read or a write depending on whether it has a $out stage.
        // $out is required to be the last stage of the pipeline.
        var stages = cmdObj.pipeline;
        const lastStage = stages && Array.isArray(stages) && (stages.length !== 0)
            ? stages[stages.length - 1]
            : undefined;
        const hasOut =
            lastStage && (typeof lastStage === "object") && lastStage.hasOwnProperty("$out");
        const hasExplain = cmdObj.hasOwnProperty("explain");

        if (!hasExplain && !hasOut) {
            supportsMajorityReadConcern = true;
        }
    }

    return supportsMajorityReadConcern;
};

Mongo.prototype.setSlaveOk = function(value) {
    if (value == undefined)
        value = true;
    this.slaveOk = value;
};

Mongo.prototype.getSlaveOk = function() {
    return this.slaveOk || false;
};

Mongo.prototype.getDB = function(name) {
    if ((jsTest.options().keyFile) &&
        ((typeof this.authenticated == 'undefined') || !this.authenticated)) {
        jsTest.authenticate(this);
    }
    // There is a weird issue where typeof(db._name) !== "string" when the db name
    // is created from objects returned from native C++ methods.
    // This hack ensures that the db._name is always a string.
    if (typeof(name) === "object") {
        name = name.toString();
    }
    return new DB(this, name);
};

Mongo.prototype.getDBs = function() {
    var res = this.adminCommand({"listDatabases": 1});
    if (!res.ok)
        throw _getErrorWithCode(res, "listDatabases failed:" + tojson(res));
    return res;
};

/**
 *  Adds afterClusterTime to the readConcern.
 */
Mongo.prototype._injectAfterClusterTime = function(cmdObj) {
    cmdObj = Object.assign({}, cmdObj);
    // The operationTime returned by the current session (i.e. connection) is the
    // smallest time that is needed for causal consistent read. The clusterTime is >=
    // the operationTime so it's less efficient to wait on the server for the
    // clusterTime.
    const operationTime = this.getOperationTime();
    if (operationTime) {
        const readConcern = Object.assign({}, cmdObj.readConcern);
        // Currently server supports afterClusterTime only with level:majority. Going forward it
        // will be relaxed for any level of readConcern.
        if (!readConcern.hasOwnProperty("afterClusterTime")) {
            readConcern.afterClusterTime = operationTime;
        }
        if (!readConcern.hasOwnProperty("level")) {
            readConcern.level = "local";
        }
        cmdObj.readConcern = readConcern;
    }
    return cmdObj;
};

Mongo.prototype._gossipLogicalTime = function(obj) {
    obj = Object.assign({}, obj);
    const clusterTime = this.getClusterTime();
    if (clusterTime) {
        obj["$clusterTime"] = clusterTime;
    }
    return obj;
};

/**
 * Sets logicalTime and operationTime extracted from command reply.
 * This is applicable for the protocol starting from version 3.6.
 */
Mongo.prototype._setLogicalTimeFromReply = function(res) {
    if (res.hasOwnProperty("operationTime")) {
        this.setOperationTime(res["operationTime"]);
    }
    if (res.hasOwnProperty("$clusterTime")) {
        this.setClusterTime(res["$clusterTime"]);
    }
};

/**
 *  Adds afterClusterTime to the readConcern if its supported and runs the command.
 */
(function(original) {
    Mongo.prototype.runCommandWithMetadata = function runCommandWithMetadata(
        dbName, metadata, cmdObj) {
        if (this.isCausalConsistencyEnabled(cmdObj) && cmdObj) {
            cmdObj = this._injectAfterClusterTime(cmdObj);
        }
        if (this._isCausal) {
            metadata = this._gossipLogicalTime(metadata);
        }
        const res = original.call(this, dbName, metadata, cmdObj);
        this._setLogicalTimeFromReply(res);
        return res;
    };
})(Mongo.prototype.runCommandWithMetadata);

/**
 *  Adds afterClusterTime to the readConcern if its supported and runs the command.
 */
(function(original) {
    Mongo.prototype.runCommand = function runCommand(dbName, cmdObj, options) {
        if (this.isCausalConsistencyEnabled(cmdObj) && cmdObj) {
            cmdObj = this._injectAfterClusterTime(cmdObj);
        }
        if (this._isCausal) {
            cmdObj = this._gossipLogicalTime(cmdObj);
        }
        const res = original.call(this, dbName, cmdObj, options);
        this._setLogicalTimeFromReply(res);
        return res;
    };
})(Mongo.prototype.runCommand);

Mongo.prototype.adminCommand = function(cmd) {
    return this.getDB("admin").runCommand(cmd);
};

/**
 * Returns all log components and current verbosity values
 */
Mongo.prototype.getLogComponents = function() {
    var res = this.adminCommand({getParameter: 1, logComponentVerbosity: 1});
    if (!res.ok)
        throw _getErrorWithCode(res, "getLogComponents failed:" + tojson(res));
    return res.logComponentVerbosity;
};

/**
 * Accepts optional second argument "component",
 * string of form "storage.journaling"
 */
Mongo.prototype.setLogLevel = function(logLevel, component) {
    componentNames = [];
    if (typeof component === "string") {
        componentNames = component.split(".");
    } else if (component !== undefined) {
        throw Error("setLogLevel component must be a string:" + tojson(component));
    }
    var vDoc = {verbosity: logLevel};

    // nest vDoc
    for (var key, obj; componentNames.length > 0;) {
        obj = {};
        key = componentNames.pop();
        obj[key] = vDoc;
        vDoc = obj;
    }
    var res = this.adminCommand({setParameter: 1, logComponentVerbosity: vDoc});
    if (!res.ok)
        throw _getErrorWithCode(res, "setLogLevel failed:" + tojson(res));
    return res;
};

Mongo.prototype.getDBNames = function() {
    return this.getDBs().databases.map(function(z) {
        return z.name;
    });
};

Mongo.prototype.getCollection = function(ns) {
    var idx = ns.indexOf(".");
    if (idx < 0)
        throw Error("need . in ns");
    var db = ns.substring(0, idx);
    var c = ns.substring(idx + 1);
    return this.getDB(db).getCollection(c);
};

Mongo.prototype.toString = function() {
    return "connection to " + this.host;
};
Mongo.prototype.tojson = Mongo.prototype.toString;

/**
 * Sets the read preference.
 *
 * @param mode {string} read preference mode to use. Pass null to disable read
 *     preference.
 * @param tagSet {Array.<Object>} optional. The list of tags to use, order matters.
 *     Note that this object only keeps a shallow copy of this array.
 */
Mongo.prototype.setReadPref = function(mode, tagSet) {
    if ((this._readPrefMode === "primary") && (typeof(tagSet) !== "undefined") &&
        (Object.keys(tagSet).length > 0)) {
        // we allow empty arrays/objects or no tagSet for compatibility reasons
        throw Error("Can not supply tagSet with readPref mode primary");
    }
    this._setReadPrefUnsafe(mode, tagSet);
};

// Set readPref without validating. Exposed so we can test the server's readPref validation.
Mongo.prototype._setReadPrefUnsafe = function(mode, tagSet) {
    this._readPrefMode = mode;
    this._readPrefTagSet = tagSet;
};

Mongo.prototype.getReadPrefMode = function() {
    return this._readPrefMode;
};

Mongo.prototype.getReadPrefTagSet = function() {
    return this._readPrefTagSet;
};

// Returns a readPreference object of the type expected by mongos.
Mongo.prototype.getReadPref = function() {
    var obj = {}, mode, tagSet;
    if (typeof(mode = this.getReadPrefMode()) === "string") {
        obj.mode = mode;
    } else {
        return null;
    }
    // Server Selection Spec: - if readPref mode is "primary" then the tags field MUST
    // be absent. Ensured by setReadPref.
    if (Array.isArray(tagSet = this.getReadPrefTagSet())) {
        obj.tags = tagSet;
    }

    return obj;
};

/**
 * Sets the read concern.
 *
 * @param level {string} read concern level to use. Pass null to disable read concern.
 */
Mongo.prototype.setReadConcern = function(level) {
    if (!level) {
        this._readConcernLevel = undefined;
    } else if (level === "local" || level === "majority") {
        this._readConcernLevel = level;
    } else {
        throw Error("Invalid read concern.");
    }
};

/**
 * Gets the read concern.
 */
Mongo.prototype.getReadConcern = function() {
    return this._readConcernLevel;
};

connect = function(url, user, pass) {
    if (url instanceof MongoURI) {
        user = url.user;
        pass = url.password;
        url = url.uri;
    }
    if (user && !pass)
        throw Error("you specified a user and not a password.  " +
                    "either you need a password, or you're using the old connect api");

    // Validate connection string "url" as "hostName:portNumber/databaseName"
    //                                  or "hostName/databaseName"
    //                                  or "databaseName"
    //                                  or full mongo uri.
    var urlType = typeof url;
    if (urlType == "undefined") {
        throw Error("Missing connection string");
    }
    if (urlType != "string") {
        throw Error("Incorrect type \"" + urlType + "\" for connection string \"" + tojson(url) +
                    "\"");
    }
    url = url.trim();
    if (0 == url.length) {
        throw Error("Empty connection string");
    }

    if (!url.startsWith("mongodb://")) {
        const colon = url.lastIndexOf(":");
        const slash = url.lastIndexOf("/");
        if (slash == 0) {
            throw Error("Failed to parse mongodb:// URL: " + url);
        }
        if (slash == -1 && colon == -1) {
            url = "mongodb://127.0.0.1:27017/" + url;
        } else if (slash != -1) {
            url = "mongodb://" + url;
        }
    }

    chatty("connecting to: " + url);
    var m = new Mongo(url);
    db = m.getDB(m.defaultDB);

    if (user && pass) {
        if (!db.auth(user, pass)) {
            throw Error("couldn't login");
        }
    }

    // Check server version
    var serverVersion = db.version();
    chatty("MongoDB server version: " + serverVersion);

    var shellVersion = version();
    if (serverVersion.slice(0, 3) != shellVersion.slice(0, 3)) {
        chatty("WARNING: shell and server versions do not match");
    }

    return db;
};

/** deprecated, use writeMode below
 *
 */
Mongo.prototype.useWriteCommands = function() {
    return (this.writeMode() != "legacy");
};

Mongo.prototype.forceWriteMode = function(mode) {
    this._writeMode = mode;
};

Mongo.prototype.hasWriteCommands = function() {
    var hasWriteCommands = (this.getMinWireVersion() <= 2 && 2 <= this.getMaxWireVersion());
    return hasWriteCommands;
};

Mongo.prototype.hasExplainCommand = function() {
    var hasExplain = (this.getMinWireVersion() <= 3 && 3 <= this.getMaxWireVersion());
    return hasExplain;
};

/**
 * {String} Returns the current mode set. Will be commands/legacy/compatibility
 *
 * Sends isMaster to determine if the connection is capable of using bulk write operations, and
 * caches the result.
 */

Mongo.prototype.writeMode = function() {

    if ('_writeMode' in this) {
        return this._writeMode;
    }

    // get default from shell params
    if (_writeMode)
        this._writeMode = _writeMode();

    // can't use "commands" mode unless server version is good.
    if (this.hasWriteCommands()) {
        // good with whatever is already set
    } else if (this._writeMode == "commands") {
        this._writeMode = "compatibility";
    }

    return this._writeMode;
};

/**
 * Returns true if the shell is configured to use find/getMore commands rather than the C++ client.
 *
 * Currently, the C++ client will always use OP_QUERY find and OP_GET_MORE.
 */
Mongo.prototype.useReadCommands = function() {
    return (this.readMode() === "commands");
};

/**
 * For testing, forces the shell to use the readMode specified in 'mode'. Must be either "commands"
 * (use the find/getMore commands), "legacy" (use legacy OP_QUERY/OP_GET_MORE wire protocol reads),
 * or "compatibility" (auto-detect mode based on wire version).
 */
Mongo.prototype.forceReadMode = function(mode) {
    if (mode !== "commands" && mode !== "compatibility" && mode !== "legacy") {
        throw new Error("Mode must be one of {commands, compatibility, legacy}, but got: " + mode);
    }

    this._readMode = mode;
};

/**
 * Get the readMode string (either "commands" for find/getMore commands, "legacy" for OP_QUERY find
 * and OP_GET_MORE, or "compatibility" for detecting based on wire version).
 */
Mongo.prototype.readMode = function() {
    // Get the readMode from the shell params if we don't have one yet.
    if (typeof _readMode === "function" && !this.hasOwnProperty("_readMode")) {
        this._readMode = _readMode();
    }

    if (this.hasOwnProperty("_readMode") && this._readMode !== "compatibility") {
        // We already have determined our read mode. Just return it.
        return this._readMode;
    } else {
        // We're in compatibility mode. Determine whether the server supports the find/getMore
        // commands. If it does, use commands mode. If not, degrade to legacy mode.
        try {
            var hasReadCommands = (this.getMinWireVersion() <= 4 && 4 <= this.getMaxWireVersion());
            if (hasReadCommands) {
                this._readMode = "commands";
            } else {
                this._readMode = "legacy";
            }
        } catch (e) {
            // We failed trying to determine whether the remote node supports the find/getMore
            // commands. In this case, we keep _readMode as "compatibility" and the shell should
            // issue legacy reads. Next time around we will issue another isMaster to try to
            // determine the readMode decisively.
        }
    }

    return this._readMode;
};

//
// Write Concern can be set at the connection level, and is used for all write operations unless
// overridden at the collection level.
//

Mongo.prototype.setWriteConcern = function(wc) {
    if (wc instanceof WriteConcern) {
        this._writeConcern = wc;
    } else {
        this._writeConcern = new WriteConcern(wc);
    }
};

Mongo.prototype.getWriteConcern = function() {
    return this._writeConcern;
};

Mongo.prototype.unsetWriteConcern = function() {
    delete this._writeConcern;
};

/**
 * Sets the operationTime.
 */
Mongo.prototype.setOperationTime = function(operationTime) {
    if (operationTime === Timestamp(0, 0)) {
        throw Error("Attempt to set an uninitiated operationTime");
    }
    if (this._operationTime === undefined || this._operationTime === null ||
        (typeof operationTime === "object" &&
         bsonWoCompare(operationTime, this._operationTime) === 1)) {
        this._operationTime = operationTime;
    }
};

/**
 * Gets the operationTime or null if unset.
 */
Mongo.prototype.getOperationTime = function() {
    if (this._operationTime === undefined) {
        return null;
    }
    return this._operationTime;
};

/**
 * Sets the clusterTime.
 */
Mongo.prototype.setClusterTime = function(logicalTimeObj) {
    if (typeof logicalTimeObj === "object" && logicalTimeObj.hasOwnProperty("clusterTime") &&
        (this._clusterTime === undefined || this._clusterTime === null ||
         bsonWoCompare(logicalTimeObj.clusterTime, this._clusterTime.clusterTime) === 1)) {
        this._clusterTime = logicalTimeObj;
    }
};

/**
 * Gets the clusterTime or null if unset.
 */
Mongo.prototype.getClusterTime = function() {
    if (this._clusterTime === undefined) {
        return null;
    }
    return this._clusterTime;
};
