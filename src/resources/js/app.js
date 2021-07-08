/* laminar.js
 * frontend application for Laminar Continuous Integration
 * https://laminar.ohwg.net
 */

 // A hash function added to String helps generating consistent
 // colours from job names for use in charts
String.prototype.hashCode = function() {
  for(var r=0, i=0; i<this.length; i++)
    r=(r<<5)-r+this.charCodeAt(i),r&=r;
  return r;
};

// Filter to pretty-print the size of artifacts
Vue.filter('iecFileSize', bytes => {
  const exp = Math.floor(Math.log(bytes) / Math.log(1024));
  return (bytes / Math.pow(1024, exp)).toFixed(1) + ' ' +
    ['B', 'KiB', 'MiB', 'GiB', 'TiB'][exp];
});


// Mixin for periodically updating a progress bar
Vue.mixin({
  data: () => ({ jobsRunning: [] }),
  methods: {
    updateProgress(o) {
      if (o.etc) {
        const p = (Math.floor(Date.now()/1000) + this.$root.clockSkew - o.started) / (o.etc - o.started);
        if (p > 1.2)
          o.overtime = true;
        o.progress = (p >= 1) ? 99 : 100 * p;
      }
    }
  },
  beforeDestroy: () => {
    clearInterval(this.updateTimer);
  },
  watch: {
    jobsRunning(val) {
      // this function handles several cases:
      // - the route has changed to a different run of the same job
      // - the current job has ended
      // - the current job has started (practically hard to reach)
      clearInterval(this.updateTimer);
      if (val.length) {
        // set the current progress update first
        this.jobsRunning.forEach(this.updateProgress);
        this.$forceUpdate();
        // then update with animation every second
        this.updateTimer = setInterval(() => {
          this.jobsRunning.forEach(this.updateProgress);
          this.$forceUpdate();
        }, 1000);
      }
    }
  }
});

// Utility methods
Vue.mixin({
  methods: {
    // Get an svg icon given a run result
    runIcon: result =>
      (result == 'success') ? /* checkmark */
        `<svg class="status success" viewBox="0 0 100 100">
          <path d="m 23,46 c -6,0 -17,3 -17,11 0,8 9,30 12,32 3,2 14,5 20,-2 6,-6 24,-36
           56,-71 5,-3 -9,-8 -23,-2 -13,6 -33,42 -41,47 -6,-3 -5,-12 -8,-15 z" />
         </svg>`
      : (result == 'failed' || result == 'aborted') ? /* cross */
        `<svg class="status failed" viewBox="0 0 100 100">
          <path d="m 19,20 c 2,8 12,29 15,32 -5,5 -18,21 -21,26 2,3 8,15 11,18 4,-6 17,-21
           21,-26 5,5 11,15 15,20 8,-2 15,-9 20,-15 -3,-3 -17,-18 -20,-24 3,-5 23,-26 30,-33 -3,-5 -8,-9
           -12,-12 -6,5 -26,26 -29,30 -6,-8 -11,-15 -15,-23 -3,0 -12,5 -15,7 z" />
         </svg>`
      : (result == 'queued') ? /* clock */
        `<svg class="status queued" viewBox="0 0 100 100">
          <circle r="50" cy="50" cx="50" />
          <path d="m 50,15 0,35 17,17" stroke-width="10" fill="none" />
         </svg>`
      : /* spinner */
        `<svg class="status running" viewBox="0 0 100 100">
          <circle cx="50" cy="50" r="40" stroke-width="15" fill="none" stroke-dasharray="175">
           <animateTransform attributeName="transform" type="rotate" repeatCount="indefinite" dur="2s" values="0 50 50;360 50 50"></animateTransform>
          </circle>
         </svg>`,
    // Pretty-print a unix date
    formatDate: unix => {
      // TODO: reimplement when toLocaleDateString() accepts formatting options on most browsers
      const d = new Date(1000 * unix);
      let m = d.getMinutes();
      if (m < 10)
        m = '0' + m;
      return d.getHours() + ':' + m + ' on ' +
        ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'][d.getDay()] + ' ' + d.getDate() + '. ' +
        ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'][d.getMonth()] + ' ' +
        d.getFullYear();
    },
    // Pretty-print a duration
    formatDuration: function(start, end) {
      if(!end)
        end = Math.floor(Date.now()/1000) + this.$root.clockSkew;
      if(end - start > 3600)
        return Math.floor((end-start)/3600) + ' hours, ' + Math.floor(((end-start)%3600)/60) + ' minutes';
      else if(end - start > 60)
        return Math.floor((end-start)/60) + ' minutes, ' + ((end-start)%60) + ' seconds';
      else
        return (end-start) + ' seconds';
    }
  }
});

// Chart factory
const Charts = (() => {
  // TODO usage is broken!
  const timeScale = max => max > 3600
    ? { factor: 1/3600, ticks: v => v.toFixed(1), label:'Hours' }
    : max > 60
    ? { factor: 1/60, ticks: v => v.toFixed(1), label:'Minutes' }
    : { factor: 1, ticks: v => v, label:'Seconds' };
  return {
    createExecutorUtilizationChart: (id, nBusy, nTotal) => {
      const c = new Chart(document.getElementById(id), {
        type: 'pie',
        data: {
          labels: [ "Busy", "Idle" ],
          datasets: [{
            data: [ nBusy, nTotal - nBusy ],
            backgroundColor: [ "#afa674", "#7483af" ]
          }]
        },
        options: {
          hover: { mode: null }
        }
      });
      c.executorBusyChanged = busy => {
        c.data.datasets[0].data[0] += busy ? 1 : -1;
        c.data.datasets[0].data[1] -= busy ? 1 : -1;
        c.update();
      }
      return c;
    },
    createRunsPerDayChart: (id, data) => {
      const dayNames = (() => {
        const res = [];
        var now = new Date();
        for (var i = 6; i >= 0; --i) {
          var then = new Date(now.getTime() - i * 86400000);
          res.push({
            short: ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"][then.getDay()],
            long: then.toLocaleDateString()}
          );
        }
        return res;
      })();
      const c = new Chart(document.getElementById(id), {
        type: 'line',
        data: {
          labels: dayNames.map(e => e.short),
          datasets: [{
            label: 'Failed Builds',
            backgroundColor: "#883d3d",
            data: data.map(e => e.failed || 0)
          },{
            label: 'Successful Builds',
            backgroundColor: "#74af77",
            data: data.map(e => e.success || 0)
          }]
        },
        options:{
          title: { display: true, text: 'Runs per day' },
          tooltips:{callbacks:{title: (tip, data) => dayNames[tip[0].index].long}},
          scales:{yAxes:[{
            ticks:{userCallback: (label, index, labels) => Number.isInteger(label) ? label: null},
            stacked: true
          }]}
        }
      });
      c.jobCompleted = success => {
        c.data.datasets[success ? 1 : 0].data[6]++;
        c.update();
      }
      return c;
    },
    createRunsPerJobChart: (id, data) => {
      const c = new Chart(document.getElementById("chartBpj"), {
        type: 'horizontalBar',
        data: {
          labels: Object.keys(data),
          datasets: [{
            label: 'Runs in last 24 hours',
            backgroundColor: "#7483af",
            data: Object.keys(data).map(e => data[e])
          }]
        },
        options:{
          title: { display: true, text: 'Runs per job' },
          hover: { mode: null },
          scales:{xAxes:[{ticks:{userCallback: (label, index, labels)=> Number.isInteger(label) ? label: null}}]}
        }
      });
      c.jobCompleted = name => {
        for (var j = 0; j < c.data.datasets[0].data.length; ++j) {
          if (c.data.labels[j] == name) {
            c.data.datasets[0].data[j]++;
            c.update();
            return;
          }
        }
        // if we get here, it's a new/unknown job
        c.data.labels.push(name);
        c.data.datasets[0].data.push(1);
        c.update();
      }
      return c;
    },
    createTimePerJobChart: (id, data, completedCounts) => {
      const scale = timeScale(Math.max(...Object.values(data)));
      const c = new Chart(document.getElementById(id), {
        type: 'horizontalBar',
        data: {
          labels: Object.keys(data),
          datasets: [{
            label: 'Mean run time this week',
            backgroundColor: "#7483af",
            data: Object.keys(data).map(e => data[e] * scale.factor)
          }]
        },
        options:{
          title: { display: true, text: 'Mean run time this week' },
          hover: { mode: null },
          scales:{xAxes:[{
            ticks:{userCallback: scale.ticks},
            scaleLabel: {
              display: true,
              labelString: scale.label
            }
          }]},
          tooltips:{callbacks:{
            label: (tip, data) => data.datasets[tip.datasetIndex].label + ': ' + tip.xLabel.toFixed(2) + ' ' + scale.label.toLowerCase()
          }}
        }
      });
      c.jobCompleted = (name, time) => {
        for (var j = 0; j < c.data.datasets[0].data.length; ++j) {
          if (c.data.labels[j] == name) {
            c.data.datasets[0].data[j] = ((completedCounts[name]-1) * c.data.datasets[0].data[j] + time * scale.factor) / completedCounts[name];
            c.update();
            return;
          }
        }
        // if we get here, it's a new/unknown job
        c.data.labels.push(name);
        c.data.datasets[0].data.push(time * scale.factor);
        c.update();
      };
      return c;
    },
    createRunTimeChangesChart: (id, data) => {
      const scale = timeScale(Math.max(...data.map(e => Math.max(...e.durations))));
      const dataValue = (name, durations) => ({
        label: name,
        data: durations.map(x => x * scale.factor),
        borderColor: 'hsl('+(name.hashCode() % 360)+', 27%, 57%)',
        backgroundColor: 'transparent'
      });
      const c = new Chart(document.getElementById(id), {
        type: 'line',
        data: {
          labels: [...Array(10).keys()],
          datasets: data.map(e => dataValue(e.name, e.durations))
        },
        options:{
          title: { display: true, text: 'Run time changes' },
          legend:{ display: true, position: 'bottom' },
          scales:{
            xAxes:[{ticks:{display: false}}],
            yAxes:[{
              ticks:{userCallback: scale.ticks},
              scaleLabel: {
                display: true,
                labelString: scale.label
              }
            }]
          },
          tooltips:{
            enabled:false
          }
        }
      });
      c.jobCompleted = (name, time) => {
        for (var j = 0; j < c.data.datasets.length; ++j) {
          if (c.data.datasets[j].label == name) {
            if(c.data.datasets[j].data.length == 10)
              c.data.datasets[j].data.shift();
            c.data.datasets[j].data.push(time * scale.factor);
            c.update();
            return;
          }
        }
        // if we get here, it's a new/unknown job
        c.data.datasets.push(dataValue(name, [time]));
        c.update();
      };
      return c;
    },
    createRunTimeChart: (id, jobs, avg) => {
      const scale = timeScale(Math.max(...jobs.map(v=>v.completed-v.started)));
      const c = new Chart(document.getElementById(id), {
        type: 'bar',
        data: {
          labels: jobs.map(e => '#' + e.number).reverse(),
          datasets: [{
            label: 'Average',
            type: 'line',
            data: [{x:0, y:avg * scale.factor}, {x:1, y:avg * scale.factor}],
            borderColor: '#7483af',
            backgroundColor: 'transparent',
            xAxisID: 'avg',
            pointRadius: 0,
            pointHitRadius: 0,
            pointHoverRadius: 0,
          },{
            label: 'Build time',
            backgroundColor: jobs.map(e => e.result == 'success' ? '#74af77': '#883d3d').reverse(),
            data: jobs.map(e => (e.completed - e.started) * scale.factor).reverse()
          }]
        },
        options: {
          title: { display: true, text: 'Build time' },
          hover: { mode: null },
          scales:{
            xAxes:[{
              categoryPercentage: 0.95,
              barPercentage: 1.0
            },{
              id: 'avg',
              type: 'linear',
              ticks: {
                display: false
              },
              gridLines: {
                display: false,
                drawBorder: false
              }
            }],
            yAxes:[{
              ticks:{userCallback: scale.ticks},
              scaleLabel:{display: true, labelString: scale.label}
            }]
          },
          tooltips:{callbacks:{
            label: (tip, data) => scale.ticks(tip.yLabel) + ' ' + scale.label.toLowerCase()
          }}
        }
      });
      c.jobCompleted = (num, result, time) => {
        let avg = c.data.datasets[0].data[0].y / scale.factor;
        avg = ((avg * (num - 1)) + time) / num;
        c.data.datasets[0].data[0].y = avg * scale.factor;
        c.data.datasets[0].data[1].y = avg * scale.factor;
        if(c.data.datasets[1].data.length == 20) {
          c.data.labels.shift();
          c.data.datasets[1].data.shift();
          c.data.datasets[1].backgroundColor.shift();
        }
        c.data.labels.push('#' + num);
        c.data.datasets[1].data.push(time * scale.factor);
        c.data.datasets[1].backgroundColor.push(result == 'success' ? '#74af77': '#883d3d');
        c.update();
      };
      return c;
    }
  };
})();

// For all charts, set miniumum Y to 0
Chart.scaleService.updateScaleDefaults('linear', {
    ticks: { suggestedMin: 0 }
});
// Don't display legend by default
Chart.defaults.global.legend.display = false;
// Disable tooltip hover animations
Chart.defaults.global.hover.animationDuration = 0;

// Component for the / endpoint
const Home = templateId => {
  const state = {
    jobsQueued: [],
    jobsRecent: [],
    resultChanged: [],
    lowPassRates: [],
  };
  let chtUtilization, chtBuildsPerDay, chtBuildsPerJob, chtTimePerJob;
  let completedCounts;
  return {
    template: templateId,
    data: () => state,
    methods: {
      status: function(msg) {
        state.jobsQueued = msg.queued;
        state.jobsRunning = msg.running;
        state.jobsRecent = msg.recent;
        state.resultChanged = msg.resultChanged;
        state.lowPassRates = msg.lowPassRates;
        completedCounts = msg.completedCounts;
        this.$forceUpdate();

        // defer charts to nextTick because they get DOM elements which aren't rendered yet
        this.$nextTick(() => {
          chtUtilization = Charts.createExecutorUtilizationChart("chartUtil", msg.executorsBusy, msg.executorsTotal);
          chtBuildsPerDay = Charts.createRunsPerDayChart("chartBpd", msg.buildsPerDay);
          chtBuildsPerJob = Charts.createRunsPerJobChart("chartBpj", msg.buildsPerJob);
          chtTimePerJob = Charts.createTimePerJobChart("chartTpj", msg.timePerJob, completedCounts);
          chtBuildTimeChanges = Charts.createRunTimeChangesChart("chartBuildTimeChanges", msg.buildTimeChanges);
        });
      },
      job_queued: function(data) {
        state.jobsQueued.splice(0, 0, data);
        this.$forceUpdate();
      },
      job_started: function(data) {
        state.jobsQueued.splice(state.jobsQueued.length - data.queueIndex - 1, 1);
        state.jobsRunning.splice(0, 0, data);
        this.$forceUpdate();
        chtUtilization.executorBusyChanged(true);
      },
      job_completed: function(data) {
        if(!(job.name in completedCounts))
          completedCounts[job.name] = 0;
        for(let i = 0; i < state.jobsRunning.length; ++i) {
          const job = state.jobsRunning[i];
          if (job.name == data.name && job.number == data.number) {
            state.jobsRunning.splice(i, 1);
            state.jobsRecent.splice(0, 0, data);
            this.$forceUpdate();
            break;
          }
        }
        for(let i = 0; i < state.resultChanged.length; ++i) {
          const job = state.resultChanged[i];
          if(job.name == data.name) {
            job[data.result === 'success' ? 'lastSuccess' : 'lastFailure'] = data.number;
            this.$forceUpdate();
            break;
          }
        }
        for(let i = 0; i < state.lowPassRates.length; ++i) {
          const job = state.lowPassRates[i];
          if(job.name == data.name) {
            job.passRate = ((completedCounts[job.name] - 1) * job.passRate + (data.result === 'success' ? 1 : 0)) / completedCounts[job.name];
            this.$forceUpdate();
            break;
          }
        }
        completedCounts[job.name]++;
        chtBuildsPerDay.jobCompleted(data.result === 'success')
        chtUtilization.executorBusyChanged(false);
        chtBuildsPerJob.jobCompleted(data.name);
        chtTimePerJob.jobCompleted(data.name, data.completed - data.started);
        chtBuildTimeChanges.jobCompleted(data.name, data.completed - data.started);
      }
    }
  };
};

// Component for the /jobs and /wallboard endpoints
const All = templateId => {
  const state = {
    jobs: [],
    search: '',
    groups: {},
    regexps: {},
    group: null,
    ungrouped: []
  };
  return {
    template: templateId,
    data: () => state,
    methods: {
      status: function(msg) {
        state.jobs = msg.jobs;
        state.jobsRunning = msg.running;
        // mix running and completed jobs
        msg.running.forEach(job => {
          const idx = state.jobs.findIndex(j => j.name === job.name);
          if (idx > -1)
            state.jobs[idx] = job;
          else {
            // special case: first run of a job.
            state.jobs.unshift(j);
            state.jobs.sort((a, b) => a.name < b.name ? -1 : a.name > b.name ? 1 : 0);
          }
        });
        state.groups = {};
        Object.keys(msg.groups).forEach(k => state.regexps[k] = new RegExp(state.groups[k] = msg.groups[k]));
        state.ungrouped = state.jobs.filter(j => !Object.values(state.regexps).some(r => r.test(j.name))).map(j => j.name);
        state.group = state.ungrouped.length ? null : Object.keys(state.groups)[0];
      },
      job_started: function(data) {
        data.result = 'running'; // for wallboard css
        // jobsRunning must be maintained for ProgressUpdater
        let updAt = state.jobsRunning.findIndex(j => j.name === data.name);
        if (updAt === -1) {
          state.jobsRunning.unshift(data);
        } else {
          state.jobsRunning[updAt] = data;
        }
        updAt = state.jobs.findIndex(j => j.name === data.name);
        if (updAt === -1) {
          // first execution of new job. TODO insert without resort
          state.jobs.unshift(data);
          state.jobs.sort((a, b) => a.name < b.name ? -1 : a.name > b.name ? 1 : 0);
          if(!Object.values(state.regexps).some(r => r.test(data.name)))
              state.ungrouped.push(data.name);
        } else {
          state.jobs[updAt] = data;
        }
        this.$forceUpdate();
      },
      job_completed: function(data) {
        let updAt = state.jobs.findIndex(j => j.name === data.name);
        if (updAt > -1)
          state.jobs[updAt] = data;
        updAt = state.jobsRunning.findIndex(j => j.name === data.name);
        if (updAt > -1) {
          state.jobsRunning.splice(updAt, 1);
          this.$forceUpdate();
        }
      },
      filteredJobs: function() {
        let ret = [];
        if (state.group)
          ret = state.jobs.filter(job => state.regexps[state.group].test(job.name));
        else
          ret = state.jobs.filter(job => state.ungrouped.includes(job.name));
        if (this.search)
          ret = ret.filter(job => job.name.indexOf(this.search) > -1);
        return ret;
      },
      wallboardJobs: function() {
        let ret = [];
        const expr = (new URLSearchParams(window.location.search)).get('filter');
        if (expr)
          ret = state.jobs.filter(job => (new RegExp(expr)).test(job.name));
        else
          ret = state.jobs;
        // sort failed before success, newest first
        ret.sort((a,b) => a.result == b.result ? a.started - b.started : 2*(b.result == 'success')-1);
        return ret;
      },
      wallboardLink: function() {
        return 'wallboard' + (state.group ? '?filter=' + state.groups[state.group] : '');
      }
    }
  };
};

// Component for the /job/:name endpoint
const Job = templateId => {
  const state = {
    description: '',
    jobsRunning: [],
    jobsRecent: [],
    lastSuccess: null,
    lastFailed: null,
    nQueued: 0,
    pages: 0,
    sort: {}
  };
  let chtBuildTime = null;
  return {
    template: templateId,
    props: ['route'],
    data: () => state,
    methods: {
      status: function(msg) {
        state.description = msg.description;
        state.jobsRunning = msg.running;
        state.jobsRecent = msg.recent;
        state.lastSuccess = msg.lastSuccess;
        state.lastFailed = msg.lastFailed;
        state.nQueued = msg.nQueued;
        state.pages = msg.pages;
        state.sort = msg.sort;

        // "status" comes again if we change page/sorting. Delete the
        // old chart and recreate it to prevent flickering of old data
        if(chtBuildTime)
          chtBuildTime.destroy();

        // defer chart to nextTick because they get DOM elements which aren't rendered yet
        this.$nextTick(() => {
          chtBuildTime = Charts.createRunTimeChart("chartBt", msg.recent, msg.averageRuntime);
        });
      },
      job_queued: function() {
        state.nQueued++;
      },
      job_started: function(data) {
        state.nQueued--;
        state.jobsRunning.splice(0, 0, data);
        this.$forceUpdate();
      },
      job_completed: function(data) {
        const i = state.jobsRunning.findIndex(j => j.number === data.number);
        if (i > -1) {
            state.jobsRunning.splice(i, 1);
            state.jobsRecent.splice(0, 0, data);
            this.$forceUpdate();
        }
        chtBuildTime.jobCompleted(data.number, data.result, data.completed - data.started);
      },
      page_next: function() {
        state.sort.page++;
        this.query(state.sort)
      },
      page_prev: function() {
        state.sort.page--;
        this.query(state.sort)
      },
      do_sort: function(field) {
        if(state.sort.field == field) {
          state.sort.order = state.sort.order == 'asc' ? 'dsc' : 'asc';
        } else {
          state.sort.order = 'dsc';
          state.sort.field = field;
        }
        this.query(state.sort)
      },
      query: function(q) {
        this.$root.$emit('navigate', q);
      }
    }
  };
};

// Component for the /job/:name/:number endpoint
const Run = templateId => {
  const utf8decoder = new TextDecoder('utf-8');
  const ansi_up = new AnsiUp;
  ansi_up.use_classes = true;
  const state = {
    job: { artifacts: [], upstream: {} },
    latestNum: null,
    log: '',
  };
  const logFetcher = (vm, name, num) => {
    const abort = new AbortController();
    fetch('log/'+name+'/'+num, {signal:abort.signal}).then(res => {
      // ATOW pipeThrough not supported in Firefox
      //const reader = res.body.pipeThrough(new TextDecoderStream).getReader();
      const reader = res.body.getReader();
      return function pump() {
        return reader.read().then(({done, value}) => {
          value = utf8decoder.decode(value);
          if (done)
            return;
          state.log += ansi_up.ansi_to_html(
            value.replace(/\033\[\{([^:]+):(\d+)\033\\/g, (m, $1, $2) =>
                   '<a href="jobs/'+$1+'" onclick="return vroute(this);">'+$1+'</a>:'+
                   '<a href="jobs/'+$1+'/'+$2+'" onclick="return vroute(this);">#'+$2+'</a>'
                 )
          );
          vm.$forceUpdate();
          return pump();
        });
      }();
    }).catch(e => {});
    return abort;
  }
  return {
    template: templateId,
    data: () => state,
    props: ['route'],
    methods: {
      status: function(data) {
        // Check for the /latest endpoint
        const params = this._props.route.params;
        if(params.number === 'latest')
          return this.$router.replace('jobs/' + params.name + '/' + data.latestNum);

        state.number = parseInt(params.number);
        state.jobsRunning = [];
        state.job = data;
        state.latestNum = data.latestNum;
        state.jobsRunning = [data];
        state.log = '';
        if(this.logstream)
          this.logstream.abort();
        if(data.started)
          this.logstream = logFetcher(this, params.name, params.number);
      },
      job_queued: function(data) {
        state.latestNum = data.number;
        this.$forceUpdate();
      },
      job_started: function(data) {
        if(data.number === state.number) {
          state.job = Object.assign(state.job, data);
          state.job.result = 'running';
          if(this.logstream)
            this.logstream.abort();
          this.logstream = logFetcher(this, data.name, data.number);
          this.$forceUpdate();
        }
      },
      job_completed: function(data) {
        if(data.number === state.number) {
          state.job = Object.assign(state.job, data);
          state.jobsRunning = [];
          this.$forceUpdate();
        }
      },
      runComplete: function(run) {
        return !!run && (run.result === 'aborted' || run.result === 'failed' || run.result === 'success');
      },
    }
  };
};

Vue.component('RouterLink', {
  name: 'router-link',
  props: {
    to:  { type: String },
    tag: { type: String, default: 'a' }
  },
  template: `<component :is="tag" @click="navigate" :href="to"><slot></slot></component>`,
  methods: {
    navigate: function(e) {
      e.preventDefault();
      history.pushState(null, null, this.to);
      this.$root.$emit('navigate');
    }
  }
});

Vue.component('RouterView', (() => {
  const routes = [
    { path: /^$/,                   component: Home('#home') },
    { path: /^jobs$/,               component:  All('#jobs') },
    { path: /^wallboard$/,          component:  All('#wallboard') },
    { path: /^jobs\/(?<name>[^\/]+)$/,         component:  Job('#job') },
    { path: /^jobs\/(?<name>[^\/]+)\/(?<number>\d+)$/, component:  Run('#run') }
  ];

  const resolveRoute = path => {
    for(i in routes) {
      const r = routes[i].path.exec(path);
      if(r)
        return [routes[i].component, r.groups];
    }
  }

  let eventSource = null;

  const setupEventSource = (view, query) => {
    // drop any existing event source
    if(eventSource)
      eventSource.close();

    const path = (location.origin+location.pathname).substr(document.head.baseURI.length);
    const search = query ? '?' + Object.entries(query).map(([k,v])=>`${k}=${v}`).join('&') : '';

    eventSource = new EventSource(document.head.baseURI + path + search);
    eventSource.reconnectInterval = 500;
    eventSource.onmessage = msg => {
      msg = JSON.parse(msg.data);
      if(msg.type === 'status') {
        // Event source is connected. Update static data
        document.title = view.$root.title = msg.title;
        view.$root.version = msg.version;
        // Calculate clock offset (used by ProgressUpdater)
        view.$root.clockSkew = msg.time - Math.floor((new Date()).getTime()/1000);
        view.$root.connected = true;
        [view.currentView, route.params] = resolveRoute(path);
        // the component won't be instantiated until nextTick
        view.$nextTick(() => {
          // component is ready, update it with the data from the eventsource
          eventSource.comp = view.$children[0];
          // and finally run the component handler
          eventSource.comp[msg.type](msg.data);
        });
      } else {
        // at this point, the component must be defined
        if (!eventSource.comp)
          return console.error("Page component was undefined");
        view.$root.connected = true;
        view.$root.showNotify(msg.type, msg.data);
        if(typeof eventSource.comp[msg.type] === 'function')
          eventSource.comp[msg.type](msg.data);
      }
    }
    eventSource.onerror = err => {
      let ri = eventSource.reconnectInterval;
      view.$root.connected = false;
      setTimeout(() => {
        setupEventSource(view);
        if(ri < 7500)
          ri *= 1.5;
        eventSource.reconnectInterval = ri
      }, ri);
      eventSource.close();
    }
  };

  let route = {};

  return {
    name: 'router-view',
    template: `<component :is="currentView" :route="route"></component>`,
    data: () => ({
      currentView: routes[0].component, // default to home
      route: route
    }),
    created: function() {
      this.$root.$on('navigate', query => {
        setupEventSource(this, query);
      });
      window.addEventListener('popstate', () => {
        this.$root.$emit('navigate');
      });
      // initial navigation
      this.$root.$emit('navigate');
    }
  };
})());

new Vue({
  el: '#app',
  data: {
    title: '', // populated by status message
    version: '',
    clockSkew: 0,
    connected: false,
    notify: 'localStorage' in window && localStorage.getItem('showNotifications') == 1,
    route: { path: '', params: {} }
  },
  computed: {
    supportsNotifications: () =>
      'Notification' in window && Notification.permission !== 'denied'
  },
  methods: {
    toggleNotifications: function(en) {
      if(Notification.permission !== 'granted')
        Notification.requestPermission(p => this.notify = (p === 'granted'))
      else
        this.notify = en;
    },
    showNotify: function(msg, data) {
      if(this.notify && msg === 'job_completed')
        new Notification('Job ' + data.result, {
          body: data.name + ' ' + '#' + data.number + ': ' + data.result
        });
    }
  },
  watch: {
    notify: e => localStorage.setItem('showNotifications', e ? 1 : 0)
  }
});
