import os
import re
import subprocess
bench_mark_names = ['gcc','li','perl','ijpeg']
branch_pred = ['2lev','bimod','taken','nottaken','comb']
open("stats/mispredict_stats.txt","w").close()
for bench_mark_name in bench_mark_names:
    with open("stats/mispredict_stats.txt","a+") as stats:
            stats.write(bench_mark_name+"\n");
    for temp_branch_pred in branch_pred:
        cmd =  './Run.pl -db ./bench.db -dir results/gcc1 -benchmark '+bench_mark_name+' -sim /home/rmb/ACA1/simulator/ss3/sim-outorder -args \"-fastfwd 5000000 -max:inst 1000000 -bpred '+temp_branch_pred+'\" >& results/'+bench_mark_name+temp_branch_pred+'.out'
        print cmd
        subprocess.call(cmd,shell=True)
        with open('results/'+bench_mark_name+temp_branch_pred+'.out',"r") as contents:
            data = contents.read()
        miss=re.search('(bpred_.+\.misses.*)',data).group(0)
        number_of_misses = re.search('([\s]+[0-9]+)',miss).group(0).strip()
        instructions=re.search('sim_num_insn.*',data).group(0)
        number_of_instructions = re.search('([\s]+[0-9]+)',instructions).group(0).strip()
        print number_of_instructions
        print number_of_misses
        MPKI = float(int(number_of_misses)*1000)/int(number_of_instructions)
        with open("stats/mispredict_stats.txt","a+") as stats:
            stats.write(temp_branch_pred+" "+str(MPKI)+"\n")
     
