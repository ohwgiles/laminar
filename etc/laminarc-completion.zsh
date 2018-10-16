#compdef laminarc
#autoload

_laminarc() {
	if (( CURRENT == 2 )); then
		_values "Operation" \
			"queue" \
			"start" \
			"run" \
			"set" \
			"show-jobs" \
			"show-queued" \
			"show-running" \
			"abort"
	else
		case "${words[2]}" in
			queue|start|run)
				if (( CURRENT == 3 )); then
					_values "Jobs" $(laminarc show-jobs)
				fi
				;;
			abort)
				if (( CURRENT == 3 )); then
					_values "Jobs" $(laminarc show-running | cut -d : -f 1)
				elif (( CURRENT == 4 )); then
					_values "Runs" $(laminarc show-running | cut -d : -f 2)
				fi
				;;
		esac
	fi
}

_laminarc
# vim: ft=zsh
