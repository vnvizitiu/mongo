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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kNetwork

#include "mongo/platform/basic.h"

#include "mongo/transport/service_entry_point_impl.h"

#include <vector>

#include "mongo/db/auth/restriction_environment.h"
#include "mongo/transport/service_entry_point_utils.h"
#include "mongo/transport/service_state_machine.h"
#include "mongo/transport/session.h"
#include "mongo/util/log.h"
#include "mongo/util/processinfo.h"
#include "mongo/util/scopeguard.h"

namespace mongo {

void ServiceEntryPointImpl::startSession(transport::SessionHandle session) {
    // Setup the restriction environment on the Session, if the Session has local/remote Sockaddrs
    const auto& remoteAddr = session->remote().sockAddr();
    const auto& localAddr = session->local().sockAddr();
    invariant(remoteAddr && localAddr);
    auto restrictionEnvironment =
        stdx::make_unique<RestrictionEnvironment>(*remoteAddr, *localAddr);
    RestrictionEnvironment::set(session, std::move(restrictionEnvironment));

    SSMListIterator ssmIt;

    const auto sync = (_svcCtx->getServiceExecutor() == nullptr);
    auto ssm = ServiceStateMachine::create(_svcCtx, std::move(session), sync);
    {
        stdx::lock_guard<decltype(_sessionsMutex)> lk(_sessionsMutex);
        ssmIt = _sessions.emplace(_sessions.begin(), ssm);
    }

    ssm->setCleanupHook([this, ssmIt] {
        stdx::lock_guard<decltype(_sessionsMutex)> lk(_sessionsMutex);
        _sessions.erase(ssmIt);
    });

    if (!sync) {
        dassert(_svcCtx->getServiceExecutor());
        ssm->scheduleNext();
        return;
    }

    launchServiceWorkerThread([ this, ssm = std::move(ssm) ]() mutable {
        _nWorkers.addAndFetch(1);
        const auto guard = MakeGuard([this, &ssm] { _nWorkers.subtractAndFetch(1); });

        const auto numCores = [] {
            ProcessInfo p;
            if (auto availCores = p.getNumAvailableCores()) {
                return static_cast<unsigned>(*availCores);
            }
            return static_cast<unsigned>(p.getNumCores());
        }();

        while (ssm->state() != ServiceStateMachine::State::Ended) {
            ssm->runNext();

            /*
             * In perf testing we found that yielding after running a each request produced
             * at 5% performance boost in microbenchmarks if the number of worker threads
             * was greater than the number of available cores.
             */
            if (_nWorkers.load() > numCores)
                stdx::this_thread::yield();
        }
    });
}

void ServiceEntryPointImpl::endAllSessions(transport::Session::TagMask tags) {
    SSMList connsToEnd;

    // While holding the _sesionsMutex, loop over all the current connections, and if their tags
    // do not match the requested tags to skip, create a copy of their shared_ptr and place it in
    // connsToEnd.
    //
    // This will ensure that sessions to be ended will live at least long enough for us to call
    // their terminate() function, even if they've already ended because of an i/o error.
    {
        stdx::unique_lock<decltype(_sessionsMutex)> lk(_sessionsMutex);
        for (auto& ssm : _sessions) {
            if (ssm->session()->getTags() & tags) {
                log() << "Skip closing connection for connection # " << ssm->session()->id();
            } else {
                connsToEnd.emplace_back(ssm);
            }
        }
    }

    // Loop through all the connections we marked for ending and call terminate on them. They will
    // then remove themselves from _sessions whenever they transition to the next state.
    //
    // If they've already ended, then this is a noop, and the SSM will be destroyed when connsToEnd
    // goes out of scope.
    for (auto& ssm : connsToEnd) {
        ssm->terminate();
    }
}

std::size_t ServiceEntryPointImpl::getNumberOfConnections() const {
    stdx::unique_lock<decltype(_sessionsMutex)> lk(_sessionsMutex);
    return _sessions.size();
}

}  // namespace mongo
