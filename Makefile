
TARGET ?= qemu

all:  bootloader kernel userland toolchain image

bootloader:
	cd Bootloader; make all

kernel:
	cd Kernel; make all

userland:
	cd Userland; make all

toolchain:
	cd Toolchain; make all

image: kernel bootloader userland toolchain
	$(MAKE) -C Image TARGET=$(TARGET) all

# Convenience targets for explicit images
image-qemu: TARGET=qemu
image-qemu: image

image-vbox: TARGET=vbox
image-vbox: image

image-usb: TARGET=usb
image-usb: image

buddy:
	$(MAKE) MM=BUDDY all

clean:
	cd Bootloader; make clean
	cd Image; make clean
	cd Kernel; make clean
	cd Toolchain; make clean
	cd Userland; make clean

# ── Reglas para correr en el host ─────────────────────────────────────────────
# all y buddy quedan reservadas para compilar dentro de la imagen de la catedra.
# Las siguientes corren fuera del contenedor y delegan en los scripts del repo.

DOCKER_IMAGE = agodio/itba-so-multiarch:3.1

docker-pull:
	docker pull $(DOCKER_IMAGE)

docker-create:
	./create.sh

docker-compile:
	MM=FF ./compile.sh $(TARGET)

docker-compile-buddy:
	MM=BUDDY ./compile.sh $(TARGET)

qemu:
	./run.sh $(TARGET)

.PHONY: bootloader image image-qemu image-vbox image-usb collections kernel userland toolchain all buddy clean
.PHONY: docker-pull docker-create docker-compile docker-compile-buddy qemu
