# DNS Server Makefile
# 用法:
#   make             - Debug x64 编译
#   make release     - Release x64 编译
#   make clean       - 清理编译产物
#   make rebuild     - 清理后重新编译

# ===== 编译器 =====
CXX      := g++
AR       := ar
RM       := rm -rf
MKDIR    := mkdir -p
TARGET   := DNS-server

# ===== 目录 =====
SRCDIR   := .
BUILDDIR := Build
OBJDIR   := $(BUILDDIR)/obj

# ===== 源文件 (自动扫描) =====
SRCS := $(shell find . -name '*.cpp' -not -path './$(BUILDDIR)/*')

OBJS := $(SRCS:%.cpp=$(OBJDIR)/%.o)
DEPS := $(OBJS:.o=.d)

# ===== C++ 标准 =====
CXX_STD := -std=c++11

# ===== 编译选项 =====
CXXFLAGS := $(CXX_STD) -Wall -Wextra -pthread -I$(SRCDIR)
LDFLAGS  := -pthread

# Debug / Release
ifeq ($(BUILD),release)
	CXXFLAGS += -O2 -DNDEBUG
	BUILD_MODE := Release
else
	CXXFLAGS += -g -O0 -D_DEBUG
	BUILD_MODE := Debug
endif

# ===== 平台检测 =====
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
	CXXFLAGS += -DLINUX
endif
ifeq ($(UNAME_S),Darwin)
	CXXFLAGS += -DMACOS
endif

# ===== 目标 =====
.PHONY: all release clean rebuild

all: $(BUILDDIR)/$(TARGET)

release:
	$(MAKE) BUILD=release all

$(BUILDDIR)/$(TARGET): $(OBJS)
	@$(MKDIR) $(BUILDDIR)
	$(CXX) $(OBJS) $(LDFLAGS) -o $@
	@echo "[OK] Build complete: $@ ($(BUILD_MODE))"

# 编译 .cpp → .o，自动生成依赖
$(OBJDIR)/%.o: %.cpp
	@$(MKDIR) $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

# 引入自动依赖
-include $(DEPS)

clean:
	$(RM) $(OBJDIR) $(BUILDDIR)/$(TARGET)
	@echo "[OK] Clean complete"

rebuild: clean all
