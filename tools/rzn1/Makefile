
# Compile for a hosted environment on the target
HOST_EXTRACFLAGS  = $(patsubst -I%,-idirafter%, $(UBOOTINCLUDE)) \
                -idirafter $(srctree)/common/spl \
                -DUSE_HOSTCC -std=gnu99

hostprogs-y := pkgt_add_entry spkg_utility

always := $(hostprogs-y)
