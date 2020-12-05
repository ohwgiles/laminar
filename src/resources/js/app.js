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

// Mixin handling retrieving dynamic updates from the backend
Vue.mixin((() => {
  const setupEventSource = (to, query, next, comp) => {
    const es = new EventSource(document.head.baseURI + to.path.substr(1) + query);
    es.comp = comp; // When reconnecting, we already have a component. Usually this will be null.
    es.to = to; // Save a ref, needed for adding query params for pagination.
    es.onmessage = function(msg) {
      msg = JSON.parse(msg.data);
      // "status" is the first message the server always delivers.
      // Use this to confirm the navigation. The component is not
      // created until next() is called, so creating a reference
      // for other message types must be deferred. There are some extra
      // subtle checks here. If this eventsource already has a component,
      // then this is not the first time the status message has been
      // received. If the frontend requests an update, the status message
      // should not be handled here, but treated the same as any other
      // message. An exception is if the connection has been lost - in
      // that case we should treat this as a "first-time" status message.
      // !this.comp.es is used to test this condition.
      if (msg.type === 'status' && (!this.comp || !this.comp.es)) {
        next(comp => {
          // Set up bidirectional reference
          // 1. needed to reference the component for other msg types
          this.comp = comp;
          // 2. needed to close the ws on navigation away
          comp.es = this;
          comp.esReconnectInterval = 500;
          // Update html and nav titles
          document.title = comp.$root.title = msg.title;
          comp.$root.version = msg.version;
          // Calculate clock offset (used by ProgressUpdater)
          comp.$root.clockSkew = msg.time - Math.floor((new Date()).getTime()/1000);
          comp.$root.connected = true;
          // Component-specific callback handler
          comp[msg.type](msg.data, to.params);
        });
      } else {
        // at this point, the component must be defined
        if (!this.comp)
          return console.error("Page component was undefined");
        else {
          this.comp.$root.connected = true;
          this.comp.$root.showNotify(msg.type, msg.data);
          if(typeof this.comp[msg.type] === 'function')
            this.comp[msg.type](msg.data);
        }
      }
    }
    es.onerror = function(e) {
      this.comp.$root.connected = false;
      setTimeout(() => {
        // Recrate the EventSource, passing in the existing component
        this.comp.es = setupEventSource(to, query, null, this.comp);
      }, this.comp.esReconnectInterval);
      if(this.comp.esReconnectInterval < 7500)
        this.comp.esReconnectInterval *= 1.5;
      this.close();
    }
    return es;
  }
  return {
    beforeRouteEnter(to, from, next) {
      setupEventSource(to, '', (fn) => { next(fn); });
    },
    beforeRouteUpdate(to, from, next) {
      this.es.close();
      setupEventSource(to, '', (fn) => { fn(this); next(); });
    },
    beforeRouteLeave(to, from, next) {
      this.es.close();
      next();
    },
    methods: {
      query(q) {
        this.es.close();
        setupEventSource(this.es.to, '?' + Object.entries(q).map(([k,v])=>`${k}=${v}`).join('&'), fn => fn(this));
      }
    }
  };
})());

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
            break;
          }
        }
      }
      return c;
    },
    createTimePerJobChart: (id, data) => {
      const scale = timeScale(Math.max(...Object.values(data)));
      return new Chart(document.getElementById(id), {
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
            label: (tip, data) => data.datasets[tip.datasetIndex].label + ': ' + tip.xLabel + ' ' + scale.label.toLowerCase()
          }}
        }
      });
    },
    createRunTimeChangesChart: (id, data) => {
      const scale = timeScale(Math.max(...data.map(e => Math.max(...e.durations))));
      return new Chart(document.getElementById(id), {
        type: 'line',
        data: {
          labels: [...Array(10).keys()],
          datasets: data.map(e => ({
            label: e.name,
            data: e.durations.map(x => x * scale.factor),
            borderColor: 'hsl('+(e.name.hashCode() % 360)+', 27%, 57%)',
            backgroundColor: 'transparent'
          }))
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
    },
    createRunTimeChart: (id, jobs, avg) => {
      const scale = timeScale(Math.max(...jobs.map(v=>v.completed-v.started)));
      return new Chart(document.getElementById(id), {
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
        this.$forceUpdate();

        // defer charts to nextTick because they get DOM elements which aren't rendered yet
        this.$nextTick(() => {
          chtUtilization = Charts.createExecutorUtilizationChart("chartUtil", msg.executorsBusy, msg.executorsTotal);
          chtBuildsPerDay = Charts.createRunsPerDayChart("chartBpd", msg.buildsPerDay);
          chtBuildsPerJob = Charts.createRunsPerJobChart("chartBpj", msg.buildsPerJob);
          chtTimePerJob = Charts.createTimePerJobChart("chartTpj", msg.timePerJob);
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
        for (var i = 0; i < state.jobsRunning.length; ++i) {
          var job = state.jobsRunning[i];
          if (job.name == data.name && job.number == data.number) {
            state.jobsRunning.splice(i, 1);
            state.jobsRecent.splice(0, 0, data);
            this.$forceUpdate();
            break;
          }
        }
        chtBuildsPerDay.jobCompleted(data.result === 'success')
        chtUtilization.executorBusyChanged(false);
        chtBuildsPerJob.jobCompleted(data.name)
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
        return '/wallboard' + (state.group ? '?filter=' + state.groups[state.group] : '');
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
  let chtBt = null;
  return {
    template: templateId,
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
        if(chtBt)
          chtBt.destroy();

        // defer chart to nextTick because they get DOM elements which aren't rendered yet
        this.$nextTick(() => {
          chtBt = Charts.createRunTimeChart("chartBt", msg.recent, msg.averageRuntime);
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
            // TODO: update the chart
        }
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
      }
    }
  };
};

// Component for the /job/:name/:number endpoint
const Run = templateId => {
  const utf8decoder = new TextDecoder('utf-8');
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
            value.replace(/</g,'&lt;')
                 .replace(/>/g,'&gt;')
                 .replace(/\033\[\{([^:]+):(\d+)\033\\/g, (m, $1, $2) =>
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
    methods: {
      status: function(data, params) {
        // Check for the /latest endpoint
        if(params.number === 'latest')
          return this.$router.replace('/jobs/' + params.name + '/' + data.latestNum);

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

new Vue({
  el: '#app',
  data: {
    title: '', // populated by status message
    version: '',
    clockSkew: 0,
    connected: false,
    notify: 'localStorage' in window && localStorage.getItem('showNotifications') == 1
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
  },
  router: new VueRouter({
    mode: 'history',
    base: document.head.baseURI.substr(location.origin.length),
    routes: [
      { path: '/',                   component: Home('#home') },
      { path: '/jobs',               component:  All('#jobs') },
      { path: '/wallboard',          component:  All('#wallboard') },
      { path: '/jobs/:name',         component:  Job('#job') },
      { path: '/jobs/:name/:number', component:  Run('#run') }
    ],
  }),
});
