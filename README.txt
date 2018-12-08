steps to run the simulator
*****************************************************************************************************************************************************************************************************
1. Go to ss3 folder(cd ss3/).
2. Run make clean to remove all the object and unnecessary machine dependent files.
3. Run make to build the simulator.
4. Come back to the simulator folder(cd ..).
4. Inside the Run.pl file change the $exp_dir to the location where your simulator folder is present(Do pwd in simulator folder and copy what ever you get to $exp_dir)
5. Use command "./Run.pl -db ./bench.db -dir results/gcc1 -benchmark gcc -sim (value of $exp_dir)/ss3/sim-outorder -args "-fastfwd 1000000 -max:inst 1000000 -bpred bimod" >& results/x.out" 
7. You can change the benchmark you are running by just changing gcc to li or perl, can change the bpred to tage or 2lev by just replacing bimod with tage or 2lev. Here x.out is the file that gives 
the stats(like IPC or mispredict) for each run.
8.The size of the table(T1,T2 or base table in Tage)can be changed by adding -bpred:tage followed by the size of the table(<basesize><T1size><T2size><T3size><T4size>)
9.Finally counter value, tag size value changes etc can be made inside bpred.h

Optional:
You could also run mispredict_stats.py to get the stats of MPKI and IPC for each benchmark and each 
branch predictor.For this you nead python 2.7.15 and a virtual box with ubunutu installed in it.
Finally the folder stats must be present inside the simulator directory

Disclaimer:The script doesn't have command line args to change the  size of the branch predictor.It runs with the default values present in the simulator.
****************************************************************************************************************************************************************************************************


