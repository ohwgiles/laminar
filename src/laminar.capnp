@0xc2cbd510f16dab57;

interface LaminarCi {

    trigger @0 (jobName :Text, params :List(JobParam)) -> (result :MethodResult);
    start @1 (jobName :Text, params :List(JobParam)) -> (result :JobResult, buildNum :UInt32);
    set @2 (jobName :Text, buildNum :UInt32, param :JobParam) -> (result :MethodResult);
    lock @3 (lockName :Text) -> ();
    release @4 (lockName :Text) -> ();

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

