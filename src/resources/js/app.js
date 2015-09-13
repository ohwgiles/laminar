Laminar = {
	runIcon: function(result) {
		return result === "success" ? '<span style="color:forestgreen">✔</span>' : '<span style="color:crimson;">✘</span>';
	},
	jobFormatter: function(o) {
		o.duration = o.duration + "s"
		o.when = (new Date(1000 * o.started)).toLocaleString();
		return o;
	}
};
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
	.when('/jobs/:name/:num/log', {
		templateUrl: 'tpl/log.html',
		controller: 'LogController'
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
			var ws = new WebSocket("ws://" + location.host + $location.path());
			ws.onmessage = function(message) {
				callback(message.data);
			};
		}
	};
})
.controller('mainController', function($scope, $ws, $interval){
	$scope.jobsQueued = [];
	$scope.jobsRunning = [];
	$scope.jobsRecent = [];
		
	var chtUtilization, chtBuildsPerDay, chtBuildsPerJob;
	
	var updateUtilization = function(busy) {
		chtUtilization.segments[0].value += busy ? 1 : -1;
		chtUtilization.segments[1].value -= busy ? 1 : -1;
		chtUtilization.update();
	}
			
	$ws.statusListener({
		status: function(data) {
			// populate jobs
			$scope.jobsQueued = data.queued;
			$scope.jobsRunning = data.running;
			$scope.jobsRecent = data.recent.map(Laminar.jobFormatter);
			$scope.$apply();
			
			// setup charts
			chtUtilization = new Chart(document.getElementById("chartUtil").getContext("2d")).Pie(
				[{value: data.executorsBusy, color:"sandybrown", label: "Busy"},
				 {value: data.executorsTotal, color: "steelblue", label: "Idle"}],
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
					fillColor: "steelblue",
					data: Object.keys(data.buildsPerJob).map(function(e){return data.buildsPerJob[e];})
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
					$scope.jobsRecent.splice(0,0,Laminar.jobFormatter(data));
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
	$scope.active = function(url) {
		return false;
	}
	$scope.runIcon = Laminar.runIcon;
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
.controller('BrowseController', function($scope, $ws, $interval){
	$scope.jobs = [];
	$ws.statusListener({
		status: function(data) {
			$scope.jobs = data.jobs;
			$scope.$apply();
		},
	});
})
.controller('JobController', function($scope, $routeParams, $ws) {
	$scope.name = $routeParams.name;
	$scope.jobsQueued = [];
	$scope.jobsRunning = [];
	$scope.jobsRecent = [];
	$ws.statusListener({
		status: function(data) {
			$scope.jobsQueued = data.queued.filter(function(e){return e.name == $routeParams.name;});
			$scope.jobsRunning = data.running.filter(function(e){return e.name == $routeParams.name;});
			$scope.jobsRecent = data.recent.filter(function(e){return e.name == $routeParams.name;});
			$scope.$apply();
		},
		job_queued: function(data) {
			if(data.name == $routeParams.name) {
				$scope.jobsQueued.splice(0,0,data);
				$scope.$apply();
			}
		},
		job_started: function(data) {
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
	$scope.runIcon = Laminar.runIcon;
})
.controller('RunController', function($scope, $routeParams, $ws) {
	$scope.name = $routeParams.name;
	$scope.num = $routeParams.num;
	$ws.statusListener({
		status: function(data) {
			$scope.job = Laminar.jobFormatter(data);
			$scope.$apply();
		},
		job_completed: function(data) {
			$scope.job = Laminar.jobFormatter(data);
			$scope.$apply();
		}
	});
	$scope.runIcon = Laminar.runIcon;
})
.controller('LogController', function($scope, $routeParams, $ws) {
	$scope.name = $routeParams.name;
	$scope.num = $routeParams.num;
	$scope._log = ""
	$ws.logListener(function(data) {
		$scope._log += ansi_up.ansi_to_html(data);
		$scope.$apply();
		window.scrollTo(0, document.body.scrollHeight);
	});
	$scope.log = function() {
		// TODO sanitize
		return ansi_up.ansi_to_html($scope._log);
	}
	
})
.run(function() {});
