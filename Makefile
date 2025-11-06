#---------------------------------------------------------------------
# Makefile for BSGS
#
# Author : Jean-Luc PONS

ifdef gpu

SRC = SECPK1/IntGroup.cpp main.cpp SECPK1/Random.cpp \
      Timer.cpp SECPK1/Int.cpp SECPK1/IntMod.cpp \
      SECPK1/Point.cpp SECPK1/SECP256K1.cpp \
      GPU/GPUEngine.o Kangaroo.cpp HashTable.cpp \
      Backup.cpp Thread.cpp Check.cpp Network.cpp Merge.cpp PartMerge.cpp

OBJDIR = obj

OBJET = $(addprefix $(OBJDIR)/, \
      SECPK1/IntGroup.o main.o SECPK1/Random.o \
      Timer.o SECPK1/Int.o SECPK1/IntMod.o \
      SECPK1/Point.o SECPK1/SECP256K1.o \
      GPU/GPUEngine.o Kangaroo.o HashTable.o Thread.o \
      Backup.o Check.o Network.o Merge.o PartMerge.o)

else

SRC = SECPK1/IntGroup.cpp main.cpp SECPK1/Random.cpp \
      Timer.cpp SECPK1/Int.cpp SECPK1/IntMod.cpp \
      SECPK1/Point.cpp SECPK1/SECP256K1.cpp \
      Kangaroo.cpp HashTable.cpp Thread.cpp Check.cpp \
      Backup.cpp Network.cpp Merge.cpp PartMerge.cpp

OBJDIR = obj

OBJET = $(addprefix $(OBJDIR)/, \
      SECPK1/IntGroup.o main.o SECPK1/Random.o \
      Timer.o SECPK1/Int.o SECPK1/IntMod.o \
      SECPK1/Point.o SECPK1/SECP256K1.o \
      Kangaroo.o HashTable.o Thread.o Check.o Backup.o \
      Network.o Merge.o PartMerge.o)

endif

CXX        = g++
CUDA       = /usr/local/cuda
CXXCUDA    = /usr/bin/g++
NVCC       = $(CUDA)/bin/nvcc

# Default compute capabilities - supports Pascal through Blackwell
# Add/remove as needed for your GPU
ifndef ccap
COMPUTE_CAPABILITY = 60,61,70,75,80,86,89,90,100
else
COMPUTE_CAPABILITY = $(ccap)
endif

all: driverquery bsgs

ifdef gpu
ifndef ccap
driverquery:
	@echo "Attempting to auto-detect GPU compute capability..."
	@if [ -f detect_cuda.sh ]; then \
		. ./detect_cuda.sh; \
	fi
	@if [ -f cuda_version.txt ]; then \
		echo "Detected compute capability: $$(cat cuda_version.txt)"; \
	else \
		echo "Auto-detection failed, using multi-architecture build"; \
		echo "Building for compute capabilities: $(COMPUTE_CAPABILITY)"; \
	fi
else
driverquery:
	@echo "Compiling against manually selected CUDA compute capability ${ccap}"
	@echo ${ccap} > cuda_version.txt
endif
else
driverquery:
	@echo "Building CPU-only version (no GPU support)"
endif


ifdef gpu

ifdef debug
CXXFLAGS   = -DWITHGPU -m64  -mssse3 -Wno-unused-result -Wno-write-strings -g -I. -I$(CUDA)/include
else
CXXFLAGS   = -DWITHGPU -m64 -mssse3 -Wno-unused-result -Wno-write-strings -O2 -I. -I$(CUDA)/include
endif
LFLAGS     = -lpthread -L$(CUDA)/lib64 -lcudart

else

ifdef debug
CXXFLAGS   = -m64 -mssse3 -Wno-unused-result -Wno-write-strings -g -I. -I$(CUDA)/include
else
CXXFLAGS   =  -m64 -mssse3 -Wno-unused-result -Wno-write-strings -O2 -I. -I$(CUDA)/include
endif
LFLAGS     = -lpthread

endif

#--------------------------------------------------------------------

# Generate gencode flags for multiple architectures or single architecture
ifdef gpu
ifdef ccap
# Single architecture build (user-specified or auto-detected)
GENCODE_FLAGS = -gencode=arch=compute_$(ccap),code=sm_$(ccap)
else
# Multi-architecture build for broader compatibility
# Generates PTX for newest arch and SASS for each specific arch
GENCODE_FLAGS = -gencode=arch=compute_60,code=sm_60 \
                -gencode=arch=compute_61,code=sm_61 \
                -gencode=arch=compute_70,code=sm_70 \
                -gencode=arch=compute_75,code=sm_75 \
                -gencode=arch=compute_80,code=sm_80 \
                -gencode=arch=compute_86,code=sm_86 \
                -gencode=arch=compute_89,code=sm_89 \
                -gencode=arch=compute_90,code=sm_90 \
                -gencode=arch=compute_100,code=sm_100 \
                -gencode=arch=compute_100,code=compute_100
endif

# Common NVCC flags
NVCCFLAGS = -m64 -I$(CUDA)/include --compiler-options -fPIC -ccbin $(CXXCUDA)

ifdef debug
$(OBJDIR)/GPU/GPUEngine.o: GPU/GPUEngine.cu
	$(NVCC) -G -g -maxrregcount=0 --ptxas-options=-v $(NVCCFLAGS) $(GENCODE_FLAGS) -o $(OBJDIR)/GPU/GPUEngine.o -c GPU/GPUEngine.cu
else
$(OBJDIR)/GPU/GPUEngine.o: GPU/GPUEngine.cu
	$(NVCC) -O3 -maxrregcount=0 --ptxas-options=-v $(NVCCFLAGS) $(GENCODE_FLAGS) -o $(OBJDIR)/GPU/GPUEngine.o -c GPU/GPUEngine.cu
endif
endif

$(OBJDIR)/%.o : %.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $<

bsgs: $(OBJET)
	@echo Making Kangaroo-256...
	$(CXX) $(OBJET) $(LFLAGS) -o kangaroo-256

$(OBJET): | $(OBJDIR) $(OBJDIR)/SECPK1 $(OBJDIR)/GPU

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/GPU: $(OBJDIR)
	cd $(OBJDIR) && mkdir -p GPU

$(OBJDIR)/SECPK1: $(OBJDIR)
	cd $(OBJDIR) &&	mkdir -p SECPK1

clean:
	@echo Cleaning...
	@rm -f obj/*.o
	@rm -f obj/GPU/*.o
	@rm -f obj/SECPK1/*.o
	@rm -f deviceQuery/*.o
	@rm -f cuda_version.txt
	@rm -f deviceQuery/cuda_build_log.txt

