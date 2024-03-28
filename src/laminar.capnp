@0xc2cbd510f16dab57;

interface LaminarCi {

    queue @0 (jobName :Text, params :List(JobParam), frontOfQueue :Bool) -> (result :MethodResult, buildNum :UInt32);
    start @1 (jobName :Text, params :List(JobParam), frontOfQueue :Bool) -> (result :MethodResult, buildNum :UInt32);
    run @2 (jobName :Text, params :List(JobParam), frontOfQueue :Bool) -> (result :JobResult, buildNum :UInt32);
    listQueued @3 () -> (result :List(Run));
    listRunning @4 () -> (result :List(Run));
    listKnown @5 () -> (result :List(Text));
    abort @6 (run :Run) -> (result :MethodResult);
    tag @7 (run :Run, metaKey :Text, metaValue: Text) -> (result :MethodResult);

    struct Run {
        job @0 :Text;
        buildNum @1 :UInt32;
    }

    struct JobParam {
        name @0 :Text;
        value @1 :Text;
    }

    enum MethodResult {
        failed @0;
        success @1;
    }

    enum JobResult {
        unknown @0;
        failed @1;
        aborted @2;
        success @3;
    }

}

