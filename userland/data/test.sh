# A script for dash (/bin/dash /data/test.sh): the features that need fork, exec, pipes and signals.
echo "dash: $((6 * 7))"

# Pipes between three processes, a subshell, command substitution.
n=$(seq 1 20 | grep 1 | wc -l)
echo "lines with a 1: $n"
(exit 3)
echo "subshell exit status: $?"

# Functions, loops, here-document, case.
greet() { echo "hello $1"; }
for name in one two; do greet $name; done
cat <<TEXT
here: $n
TEXT
case abc in a*) echo "case matched";; esac

# Background jobs and wait.
sleep 1 &
echo "started a background job"
wait
echo "job finished"

# Redirection and exec of another program.
echo data > /tmp/dash_test.txt
cat < /tmp/dash_test.txt
/bin/echo "run by exec"

# A trap and a signal to ourselves.
trap 'echo "got USR1"' USR1
kill -USR1 $$
echo "done"
