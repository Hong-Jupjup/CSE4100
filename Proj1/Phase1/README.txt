<Project1 Phase 3: Run Processes in Background in Your Shell>

- Goal of This Phase:
	Implement commands in background using ampersand(&) at the end of the commands
	Implement additional commands such as jobs, bg, fg, kill

- Usable Commands Examples
	ls -al | grep filename &
	cat sample | grep -v a &

	jobs				List the running and stopped background jobs
	bg <job>			Change a stopped background job to a running background job
	fg <job>			Change a stopped or running background job to a running in the foreground
	kill <job>			Terminate a job
