bpred="bimod"
if [ "$1" != "" ];
then
	bpred=$1	
fi
	echo "./Run.pl -db ./bench.db -dir results/gcc1 -benchmark gcc -sim /home/rmb/ACA1/simulator/ss3/sim-outorder -args "-fastfwd 1000000 -max:inst 1000000 -bpred $bpred $2 $3 $4" >& results/gcc1_$bpred.out"
	echo$(./Run.pl -db ./bench.db -dir results/gcc1 -benchmark gcc -sim /home/rmb/ACA1/simulator/ss3/sim-outorder -args "-fastfwd 1000000 -max:inst 1000000 -bpred $bpred $2 $3 $4" >& results/gcc1_$bpred.out)

