///
/// Copyright 2015-2018 Oliver Giles
///
/// This file is part of Laminar
///
/// Laminar is free software: you can redistribute it and/or modify
/// it under the terms of the GNU General Public License as published by
/// the Free Software Foundation, either version 3 of the License, or
/// (at your option) any later version.
///
/// Laminar is distributed in the hope that it will be useful,
/// but WITHOUT ANY WARRANTY; without even the implied warranty of
/// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
/// GNU General Public License for more details.
///
/// You should have received a copy of the GNU General Public License
/// along with Laminar.  If not, see <http://www.gnu.org/licenses/>
///
#ifndef LAMINAR_RUN_H_
#define LAMINAR_RUN_H_

#include <string>
#include <queue>
#include <list>
#include <functional>
#include <ostream>
#include <unordered_map>
#include <memory>
#include <kj/async.h>
#include <kj/filesystem.h>

// Definition needed for musl
typedef unsigned int uint;

enum class RunState {
    UNKNOWN,
    QUEUED,
    RUNNING,
    ABORTED,
    FAILED,
    SUCCESS
};

std::string to_string(const RunState& rs);

class Context;

typedef std::unordered_map<std::string, std::string> ParamMap;

// Represents an execution of a job.
class Run {
public:
    Run(std::string name, uint num, ParamMap params, kj::Path&& rootPath);
    ~Run();

    // copying this class would be asking for trouble...
    Run(const Run&) = delete;
    Run& operator=(const Run&) = delete;

    kj::Promise<RunState> start(RunState lastResult, std::shared_ptr<Context> ctx, const kj::Directory &fsHome, std::function<kj::Promise<int>(kj::Maybe<pid_t>&)> getPromise);

    // aborts this run
    bool abort();

    std::string reason() const;

    kj::Promise<void> whenStarted() { return startedFork.addBranch(); }
    kj::Promise<RunState> whenFinished() { return finishedFork.addBranch(); }

    std::shared_ptr<Context> context;
    RunState result;
    std::string name;
    std::string parentName;
    int parentBuild = 0;
    uint build = 0;
    std::string log;
    kj::Maybe<pid_t> pid;
    int output_fd;
    std::unordered_map<std::string, std::string> params;
    int timeout = 0;

    time_t queuedAt;
    time_t startedAt;
private:
    // adds a script to the queue of scripts to be executed by this run
    void addScript(kj::Path scriptPath, kj::Path scriptWorkingDir, bool runOnAbort = false);

    // adds an environment file that will be sourced before this run
    void addEnv(kj::Path path);

    struct Script {
        kj::Path path;
        kj::Path cwd;
        bool runOnAbort;
    };

    kj::Path rootPath;
    std::string reasonMsg;

    kj::PromiseFulfillerPair<void> started;
    kj::ForkedPromise<void> startedFork;
    kj::PromiseFulfillerPair<RunState> finished;
    kj::ForkedPromise<RunState> finishedFork;
};

// All this below is a somewhat overengineered method of keeping track of
// currently executing builds (Run objects). This would probably scale
// very well, but it's completely gratuitous since we are not likely to
// be executing thousands of builds at the same time
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/ordered_index.hpp>

namespace bmi = boost::multi_index;

struct _run_same {
    typedef const Run* result_type;
    const Run* operator()(const std::shared_ptr<Run>& run) const {
        return run.get();
    }
};

// A single Run can be fetched by...
struct _run_index : bmi::indexed_by<
        bmi::hashed_unique<bmi::composite_key<
            std::shared_ptr<Run>,
        // a combination of their job name and build number
            bmi::member<Run, std::string, &Run::name>,
            bmi::member<Run, uint, &Run::build>
        >>,
        // or a pointer to a Run object.
        bmi::hashed_unique<_run_same>,
        // A group of Runs can be fetched by the time they started
        bmi::ordered_non_unique<bmi::member<Run, time_t, &Run::startedAt>>,
        // or by their job name
        bmi::ordered_non_unique<bmi::member<Run, std::string, &Run::name>>
    >
{};

struct RunSet: public boost::multi_index_container<
    std::shared_ptr<Run>,
    _run_index
> {
    typename bmi::nth_index<RunSet, 0>::type& byNameNumber() { return get<0>(); }
    typename bmi::nth_index<RunSet, 0>::type const& byNameNumber() const { return get<0>(); }

    typename bmi::nth_index<RunSet, 1>::type& byRunPtr() { return get<1>(); }
    typename bmi::nth_index<RunSet, 1>::type const& byRunPtr() const { return get<1>(); }

    typename bmi::nth_index<RunSet, 2>::type& byStartedAt() { return get<2>(); }
    typename bmi::nth_index<RunSet, 2>::type const& byStartedAt() const { return get<2>(); }

    typename bmi::nth_index<RunSet, 3>::type& byJobName() { return get<3>(); }
    typename bmi::nth_index<RunSet, 3>::type const& byJobName() const { return get<3>(); }
};

#endif // LAMINAR_RUN_H_
