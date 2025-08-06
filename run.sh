
set -e 

pio run -t upload -e lolin_s2_mini
sleep 2
pio device monitor -e lolin_s2_mini