COMPILER = g++
#COMPILER = clang++
#COMPILER = mpicxx
#CFLAGS   = -std=c++11 -fno-exceptions -fno-rtti -Wextra -Ofast -MMD -MP -fopenmp
CFLAGS   = -std=c++17 -fno-exceptions -fno-rtti -Wextra -Ofast -MMD -MP -fopenmp
CFLAGS   += -march=native
LDFLAGS  = -lpthread #-lboost_mpi -lboost_serialization
LIBS     =
INCLUDE  = #-I../include
ifeq ($(OS),Windows_NT)
  TARGET = apery.exe
  LDFLAGS += -static
else
  TARGET = apery
endif
OBJDIR   = ../obj
ifeq "$(strip $(OBJDIR))" ""
  OBJDIR = ..
endif
#SOURCES  = $(wildcard *.cpp)
SOURCES  = main.cpp bitboard.cpp init.cpp mt64bit.cpp position.cpp evalList.cpp \
           move.cpp movePicker.cpp square.cpp usi.cpp generateMoves.cpp evaluate.cpp \
           search.cpp hand.cpp tt.cpp timeManager.cpp book.cpp benchmark.cpp \
           thread.cpp common.cpp pieceScore.cpp \
           YaneuraOu/misc.cpp \
           YaneuraOu/extra/bitop.cpp \
           YaneuraOu/eval/evaluate_bona_piece.cpp \
           YaneuraOu/eval/nnue/evaluate_nnue.cpp \
           YaneuraOu/eval/nnue/nnue_test_command.cpp \
           YaneuraOu/eval/nnue/features/half_kp.cpp \
           YaneuraOu/eval/nnue/features/kk.cpp \
           YaneuraOu/eval/nnue/features/pp.cpp \
           YaneuraOu/eval/nnue/features/half_kp_gameply40x4.cpp \
           YaneuraOu/eval/nnue/features/half_kpkfile.cpp


OBJECTS  = $(addprefix $(OBJDIR)/, $(SOURCES:.cpp=.o))
DEPENDS  = $(OBJECTS:.o=.d)

$(TARGET): $(OBJECTS) $(LIBS)
	$(COMPILER) -o $@ $^ $(LDFLAGS) $(CFLAGS)

#$(OBJDIR)/%.o: %.cpp
#	@[ -d $(OBJDIR) ] || mkdir -p $(OBJDIR)
#	$(COMPILER) $(CFLAGS) $(INCLUDE) -o $@ -c $<

$(OBJDIR)/%.o: %.cpp
	@[ -d $(dir $@) ] || mkdir -p $(dir $@)
	$(COMPILER) $(CFLAGS) $(INCLUDE) -o $@ -c $<


all: clean $(TARGET)

bmi2:
	$(MAKE) CFLAGS='$(CFLAGS) -DNDEBUG -DHAVE_SSE4 -DHAVE_SSE42 -DHAVE_BMI2 -msse4.2 -mbmi2 -DHAVE_AVX2 -mavx2' LDFLAGS='$(LDFLAGS) -flto' $(TARGET)

sse:
	$(MAKE) CFLAGS='$(CFLAGS) -DNDEBUG -DHAVE_SSE4 -DHAVE_SSE42 -msse4.2' LDFLAGS='$(LDFLAGS) -flto' $(TARGET)

sse41:
	$(MAKE) CFLAGS='$(CFLAGS) -DNDEBUG -DHAVE_SSE4 -msse4.1' LDFLAGS='$(LDFLAGS) -flto' $(TARGET)

sse2:
	$(MAKE) CFLAGS='$(CFLAGS) -DNDEBUG -DHAVE_SSE2 -msse2' LDFLAGS='$(LDFLAGS) -flto' $(TARGET)

nosse:
	$(MAKE) CFLAGS='$(CFLAGS) -DNDEBUG' LDFLAGS='$(LDFLAGS) -flto' $(TARGET)

prof:
	$(MAKE) CFLAGS='$(CFLAGS) -pg' bmi2

profgen:
	$(MAKE) CFLAGS='$(CFLAGS) -fprofile-generate -lgcov' LDFLAGS='$(LDFLAGS) -fprofile-generate -lgcov' bmi2

profuse:
	$(MAKE) CFLAGS='$(CFLAGS) -fprofile-use -lgcov' LDFLAGS='$(LDFLAGS) -fprofile-use -lgcov' bmi2

pgo:
	$(MAKE) profgen
	@./$(TARGET) bench
	@touch $(SOURCES)
	$(MAKE) profuse

clean:
	rm -f $(OBJECTS) $(DEPENDS) $(TARGET) ${OBJECTS:.o=.gcda}

-include $(DEPENDS)
