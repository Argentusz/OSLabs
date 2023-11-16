fileName=cat.jpg
clusterSize=4096

g++ main.cpp -o main.o -lrt
touch log.txt

for opCnt in 4 8 16 32
do
  for bufSizeScale in 1 2 4 8 12 16
  do
    bufSize=$((clusterSize * bufSizeScale))
    ./main.o $opCnt $bufSize $fileName >> log.txt
    for repeat in {1..4}
    do
      ./main.o $opCnt $bufSize $fileName -short >> log.txt
    done
  done
done
