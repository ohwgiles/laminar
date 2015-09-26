angular.module('laminar',['ngRoute','ngSanitize'])
.config(function($routeProvider, $locationProvider, $sceProvider) {
	$routeProvider
	.when('/', {
		templateUrl: 'tpl/home.html',
		controller: 'mainController'
	})
	.when('/jobs', {
		templateUrl: 'tpl/browse.html',
		controller: 'BrowseController',
	})
	.when('/jobs/:name', {
		templateUrl: 'tpl/job.html',
		controller: 'JobController'
	})
	.when('/jobs/:name/:num', {
		templateUrl: 'tpl/run.html',
		controller: 'RunController'
	})
	$locationProvider.html5Mode(true);
	$sceProvider.enabled(false);
})
.factory('$ws',function($q,$location){
	return {
		statusListener: function(callbacks) {
			var ws = new WebSocket("ws://" + location.host + $location.path());
			ws.onmessage = function(message) {
				message = JSON.parse(message.data);
				callbacks[message.type](message.data);
			};
		},
		logListener: function(callback) {
			var ws = new WebSocket("ws://" + location.host + $location.path() + '/log');
			ws.onmessage = function(message) {
				callback(message.data);
			};
		}
	};
})
.controller('mainController', function($rootScope, $scope, $ws, $interval){
	$rootScope.bc = {
		nodes: [],
		current: 'Home'
	};

	$scope.jobsQueued = [];
	$scope.jobsRunning = [];
	$scope.jobsRecent = [];
	var chtUtilization, chtBuildsPerDay, chtBuildsPerJob, chtTimePerJob;
	
	var updateUtilization = function(busy) {
		chtUtilization.segments[0].value += busy ? 1 : -1;
		chtUtilization.segments[1].value -= busy ? 1 : -1;
		chtUtilization.update();
	}
			
	$ws.statusListener({
		status: function(data) {
			$rootScope.title = data.title;
			// populate jobs
			$scope.jobsQueued = data.queued;
			$scope.jobsRunning = data.running;
			$scope.jobsRecent = data.recent;

			$scope.$apply();
			
			// setup charts
			chtUtilization = new Chart(document.getElementById("chartUtil").getContext("2d")).Pie(
				[{value: data.executorsBusy, color:"tan", label: "Busy"},
				 {value: data.executorsTotal, color: "darkseagreen", label: "Idle"}],
				{animationEasing: 'easeInOutQuad'}
			);
			chtBuildsPerDay = new Chart(document.getElementById("chartBpd").getContext("2d")).Line({
				labels: function(){
					res = [];
					var now = new Date();
					for(var i = 6; i >= 0; --i) {
						var then = new Date(now.getTime() - i*86400000);
						res.push(["Sun","Mon","Tue","Wed","Thu","Fri","Sat"][then.getDay()]);
					}
					return res;
				}(),
				datasets: [{
					label: "Successful Builds",
					fillColor: "darkseagreen",
					strokeColor: "forestgreen",
					data: data.buildsPerDay.map(function(e){return e.success||0;})
				},{
					label: "Failed Bulids",
					fillColor: "darksalmon",
					strokeColor: "crimson",
					data: data.buildsPerDay.map(function(e){return e.failed||0;})
				}]},
				{ showTooltips: false }
			);
			chtBuildsPerJob = new Chart(document.getElementById("chartBpj").getContext("2d")).HorizontalBar({
				labels: Object.keys(data.buildsPerJob),
				datasets: [{
					fillColor: "lightsteelblue",
					data: Object.keys(data.buildsPerJob).map(function(e){return data.buildsPerJob[e];})
				}]
			},{});
			chtTimePerJob = new Chart(document.getElementById("chartTpj").getContext("2d")).HorizontalBar({
				labels: Object.keys(data.timePerJob),
				datasets: [{
					fillColor: "lightsteelblue",
					data: Object.keys(data.timePerJob).map(function(e){return data.timePerJob[e];})
				}]
			},{});
		},
		job_queued: function(data) {
			$scope.jobsQueued.splice(0,0,data);
			$scope.$apply();
		},
		job_started: function(data) {
			$scope.jobsQueued.splice($scope.jobsQueued.length - data.queueIndex - 1,1);
			$scope.jobsRunning.splice(0,0,data);
			$scope.$apply();
			updateUtilization(true);
		},
		job_completed: function(data) {
			if(data.result === "success")
				chtBuildsPerDay.datasets[0].points[6].value++;
			else
				chtBuildsPerDay.datasets[1].points[6].value++;
			chtBuildsPerDay.update();

			for(var i = 0; i < $scope.jobsRunning.length; ++i) {
				var job = $scope.jobsRunning[i];
				if(job.name == data.name && job.number == data.number) {
					$scope.jobsRunning.splice(i,1);
					$scope.jobsRecent.splice(0,0,data);
					$scope.$apply();
					
					break;
				}
			}
			updateUtilization(false);
			for(var j = 0; j < chtBuildsPerJob.datasets[0].bars.length; ++j) {
				if(chtBuildsPerJob.datasets[0].bars[j].label == job.name) {
					chtBuildsPerJob.datasets[0].bars[j].value++;
					chtBuildsPerJob.update();
					break;
				}
			}
		}
	});
	timeUpdater = $interval(function() {
		$scope.jobsRunning.forEach(function(o){
			if(o.etc) {
				var d = new Date();
				var p = (d.getTime()/1000 - o.started) / (o.etc - o.started);
				if(p > 1.2) {
					o.overtime = true;
				} else if(p >= 1) {
					o.progress = 99;
				} else {
					o.progress = 100 * p;
				}
			}
		});
	}, 1000);
	$scope.$on('$destroy', function() {
		$interval.cancel(timeUpdater);
	});
})
.controller('BrowseController', function($rootScope, $scope, $ws, $interval){
	$rootScope.bc = {
		nodes: [{ href: '/', label: 'Home' }],
		current: 'Jobs'
	};

	$scope.currentTag = null;
	$scope.activeTag = function(t) {
		return $scope.currentTag === t;
	};
	$scope.bytag = function(job) {
		if($scope.currentTag === null) return true;
		return job.tags.indexOf($scope.currentTag) >= 0;
	};

	$scope.jobs = [];
	$ws.statusListener({
		status: function(data) {
			$rootScope.title = data.title;
			$scope.jobs = data.jobs;
			var tags = {};
			for(var i in data.jobs) {
				for(var j in data.jobs[i].tags) {
					tags[data.jobs[i].tags[j]] = true;
				}
			}
			$scope.tags = Object.keys(tags);
			$scope.$apply();
		},
	});
})
.controller('JobController', function($rootScope, $scope, $routeParams, $ws) {
	$rootScope.bc = {
		nodes: [{ href: '/', label: 'Home' },{ href: '/jobs', label: 'Jobs' }],
		current: $routeParams.name
	};
	
	$scope.name = $routeParams.name;
	$scope.jobsRunning = [];
	$scope.jobsRecent = [];

	$ws.statusListener({
		status: function(data) {
			$rootScope.title = data.title;

			$scope.jobsRunning = data.running;
			$scope.jobsRecent = data.recent;
			$scope.lastSuccess = data.lastSuccess;
			$scope.lastFailed = data.lastFailed;
			$scope.$apply();
			
			var chtBt = new Chart(document.getElementById("chartBt").getContext("2d")).Bar({
				labels: data.recent.map(function(e){return '#' + e.number;}),
				datasets: [{
					fillColor: "darkseagreen",
					strokeColor: "forestgreen",
					data: data.recent.map(function(e){return e.duration;})
				}]
			},
			{barValueSpacing: 1,barStrokeWidth: 1,barDatasetSpacing:0}
			);

			for(var i = 0; i < data.recent.length; ++i) {
				if(data.recent[i].result != "success") {
					chtBt.datasets[0].bars[i].fillColor = "darksalmon";
					chtBt.datasets[0].bars[i].strokeColor = "crimson";
				}
			}
			chtBt.update();
			
		},
		job_queued: function() {
			$scope.nQueued++;
		},
		job_started: function(data) {
			$scope.nQueued--;
			if(data.name == $routeParams.name) {
				$scope.jobsQueued.splice($scope.jobsQueued.length - 1,1);
				$scope.jobsRunning.splice(0,0,data);
				$scope.$apply();
			}
		},
		job_completed: function(data) {
			for(var i = 0; i < $scope.jobsRunning.length; ++i) {
				var job = $scope.jobsRunning[i];
				if(job.name == data.name && job.number == data.number) {
					$scope.jobsRunning.splice(i,1);
					$scope.jobsRecent.splice(0,0,data);
					$scope.$apply();
					break;
				}
			}
		}
	});
})
.controller('RunController', function($rootScope, $scope, $routeParams, $ws) {
	$rootScope.bc = {
		nodes: [{ href: '/', label: 'Home' },
		{ href: '/jobs', label: 'Jobs' },
		{ href: '/jobs/'+$routeParams.name, label: $routeParams.name }
		],
		current: '#' + $routeParams.num
	};

	$scope.name = $routeParams.name;
	$scope.num = parseInt($routeParams.num);
	$ws.statusListener({
		status: function(data) {
			$rootScope.title = data.title;
			$scope.job = data;
			$scope.$apply();
		},
		job_started: function() {
			$scope.job.latestNum++;
			$scope.$apply();
		},
		job_completed: function(data) {
			$scope.job = data;
			$scope.$apply();
		}
	});
	
	$scope.log = ""
	$scope.autoscroll = false;
	var firstLog = false;
	$ws.logListener(function(data) {
		$scope.log += ansi_up.ansi_to_html(data.replace('<','&lt;').replace('>','&gt;'));
		$scope.$apply();
		if(!firstLog) {
			firstLog = true;
		} else if($scope.autoscroll) {
			window.scrollTo(0, document.body.scrollHeight);
		}
	});

})
.run(function($rootScope) {
	angular.extend($rootScope, {
		runIcon: function(result) {
			return result === "success" ? '<span style="color:forestgreen;font-family:\'Zapf Dingbats\';">✔</span>' : result === "failed" ? '<span style="color:crimson;">✘</span>' : '';
		},
		formatDate: function(unix) {
			// TODO reimplement when toLocaleDateString() accepts formatting
			// options on most browsers
			var d = new Date(1000 * unix);
			return d.getHours() + ':' + d.getMinutes() + ' on ' + 
				['Sun','Mon','Tue','Wed','Thu','Fri','Sat'][d.getDay()] + ' '
				+ d.getDate() + '. ' + ['Jan','Feb','Mar','Apr','May','Jun',
				'Jul','Aug','Sep', 'Oct','Nov','Dec'][d.getMonth()] + ' '
				+ d.getFullYear();
		}
	});
});
