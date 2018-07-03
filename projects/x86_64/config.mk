CONFIG_DEBUG=y
CONFIG_WERROR=y
CONFIG_OPTIMIZE=g

CONFIG_ARCH=x86_64
CONFIG_MACHINE=pc
CONFIG_TRIPLET=x86_64-pc-elf

CONFIG_UBSAN=y

CONFIG_INSTRUMENT=n

CONFIG_SERIAL_DEBUG_BAUD=38400
CONFIG_SERIAL_DEBUG_STOPBITS=1
CONFIG_SERIAL_DEBUG_WORDSZ=8
CONFIG_SERIAL_DEBUG_ENABLE=y

# set this to your toolchain path
TOOLCHAIN_PATH=/home/dbittman/code/twizzler-kernel/.tc/
QEMU_FLAGS+=-cpu host,migratable=false,host-cache-info=true
QEMU_FLAGS+="-enable-kvm"

