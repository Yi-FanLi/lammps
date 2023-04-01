DEEPMD_ROOT=/tigress/yifanl/usr/licensed/anaconda3/2021.11/envs/dpdev1
TENSORFLOW_INCLUDE_DIRS="/tigress/yifanl/usr/licensed/anaconda3/2021.11/envs/dpdev1/include;/tigress/yifanl/usr/licensed/anaconda3/2021.11/envs/dpdev1/include"
TENSORFLOW_LIBRARY_PATH="/tigress/yifanl/usr/licensed/anaconda3/2021.11/envs/dpdev1/lib"

TF_INCLUDE_DIRS=`echo $TENSORFLOW_INCLUDE_DIRS | sed "s/;/ -I/g"`
TF_LIBRARY_PATH=`echo $TENSORFLOW_LIBRARY_PATH | sed "s/;/ -L/g"`
TF_RPATH=`echo $TENSORFLOW_LIBRARY_PATH | sed "s/;/ -Wl,-rpath=/g"`

NNP_INC=" -std=c++14 -DHIGH_PREC -DLAMMPS_VERSION_NUMBER=$(./lmp_version.sh) -I$DEEPMD_ROOT/include/ "
NNP_PATH=" -L$TF_LIBRARY_PATH -L$DEEPMD_ROOT/lib"
NNP_LIB=" -Wl,--no-as-needed -ldeepmd_cc -ltensorflow_cc -ltensorflow_framework -Wl,-rpath=$TF_RPATH -Wl,-rpath=$DEEPMD_ROOT/lib"
