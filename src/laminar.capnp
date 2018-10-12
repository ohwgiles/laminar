@0xc2cbd510f16dab57;

interface LaminarCi {

    queue @0 (jobName :Text, params :List(JobParam)) -> (result :MethodResult);
    start @1 (jobName :Text, params :List(JobParam)) -> (result :MethodResult, buildNum :UInt32);
    run @2 (jobName :Text, params :List(JobParam)) -> (result :JobResult, buildNum :UInt32);
    set @3 (run :Run, param :JobParam) -> (result :MethodResult);
    listQueued @4 () -> (result :List(Text));
    listRunning @5 () -> (result :List(Run));
    listKnown @6 () -> (result :List(Text));
    abort @7 (run :Run) -> (result :MethodResult);

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

