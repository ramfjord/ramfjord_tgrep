#!/usr/bin/ruby

tgrep = "/anarchy/thomas/reddit/tgrep"
haproxy = "/anarchy/thomas/reddit/haproxy.log"

hour = ARGV[0].nil? ? 15 : ARGV[0]
min = ARGV[1].nil? ? 15 : ARGV[1]

wc_max = 0;
time_max = "nothin"

for min in 1...60
  for sec in 1...60
    wc = `strace #{tgrep} #{hour}:#{min}:#{sec} #{haproxy} 2>&1 1>/dev/null | grep -c 'lseek'`.to_i

    if wc > wc_max
      wc_max = wc
      time_max = "#{hour}:#{min}:#{sec}"
    end
  end
  puts "wc_max = #{wc_max}"
  puts "time_max = #{time_max}"
  wc_max = 0
end
