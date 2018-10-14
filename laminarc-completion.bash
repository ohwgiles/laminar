# Bash completion file for laminarc
# vim: ft=sh

_laminarc() {
	local cur prev words cword
	_init_completion || return
	if [ "$cword" -gt 1 ]; then
		case "${words[1]}" in
			queue|start|run)
				if [ "$cword" -eq 2 ]; then
					COMPREPLY+=($(compgen -W "$(laminarc show-jobs)" -- ${cur}))
				fi
				;;
			abort)
				if [ "$cword" -eq 2 ]; then
					COMPREPLY+=($(compgen -W "$(laminarc show-running | cut -d : -f 1)" -- ${cur}))
				elif [ "$cword" -eq 3 ]; then
					COMPREPLY+=($(compgen -W "$(laminarc show-running | cut -d : -f 2)" -- ${cur}))
				fi
				;;
		esac
	else
		local cmds="queue start run set show-jobs show-queued show-running abort"
		COMPREPLY+=($(compgen -W "${cmds}" -- ${cur}))
	fi
}

complete  -F _laminarc laminarc
