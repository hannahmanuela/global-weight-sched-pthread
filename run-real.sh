
# echo function_graph > /sys/kernel/debug/tracing/current_tracer
# echo pick_next_task_fair > /sys/kernel/debug/tracing/set_ftrace_filter


echo 1 > /sys/kernel/debug/tracing/tracing_on
./real &
pid=$!

sleep 2

kill $pid

echo 0 > /sys/kernel/debug/tracing/tracing_on
