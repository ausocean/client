# Install files and directories required by NetSender clients (such as gpio-netsender, rv, etc.)
# and create a dhcpcd.enter-hook for setting the MAC address.
# MA and DK can be optionally passed to Make, e.g, for a hard (first-time) installation:
# 	sudo MA=mac DK=dk install_hard
# NB: The default (soft) install does not override conf files.
USER := $(shell whoami)
PATH := /usr/local/go/bin:$(PATH)
ifeq ($(MA),)
	MA := "00:E0:4C:00:00:01"
endif
ifeq ($(DK),)
	DK := 0
endif

.SILENT:make_dirs
.SILENT:soft_copy_files
.SILENT:hard_copy_files
.SILENT:set_mac
.SILENT:syncreboot
.SILENT:clean

install: as_root make_dirs soft_copy_files
	@echo "Install complete"

install_hard: as_root make_dirs hard_copy_files set_mac
	@echo "Hard install complete"

as_root:
ifneq ($(USER),root)
	$(error Must run as superuser!)
endif

make_dirs:
	if [ ! -d /var/netsender ] ; then \
		mkdir /var/netsender; \
		chmod guo+rwx /var/netsender; \
	fi
	if [ ! -d /var/log/netsender ] ; then \
		mkdir /var/log/netsender; \
		chmod guo+rwx /var/log/netsender; \
	fi

soft_copy_files:
	echo "performing soft copy" ; \
	if [ -f /etc/systemd/system/i2c-netsender.service ] ; then \
		echo "/etc/systemd/system/i2c-netsender.service left unmodified" ; \
	else \
		cp i2c-netsender.service /etc/systemd/system; \
	fi
	systemctl enable i2c-netsender.service
	chmod +x run.sh
	if [ -f /etc/netsender.conf ] ; then \
		echo "/etc/netsender.conf left unmodified" ; \
	else \
		printf "ma $(MA)\ndk $(DK)\n" > /etc/netsender.conf; \
		chown pi /etc/netsender.conf; \
	fi

hard_copy_files:
	echo "performing hard copy" ; \
	if [ -f /etc/systemd/system/i2c-netsender.service ] ; then \
		echo "/etc/systemd/system/i2c-netsender.service overwritten" ; \
	fi
	cp -f i2c-netsender.service /etc/systemd/system
	systemctl enable i2c-netsender.service
	chmod +x run.sh
	if [ -f /etc/netsender.conf ] ; then \
		echo "Backed up netsender.conf to /etc/netsender.conf.bak"; \
		cp /etc/netsender.conf /etc/netsender.conf.bak ; \
	fi
	printf "ma $(MA)\ndk $(DK)\n" > /etc/netsender.conf
	chown pi /etc/netsender.conf

set_mac:
	printf "ip link set eth0 address $(MA)\n" > /etc/dhcpcd.enter-hook
	chmod guo+x /etc/dhcpcd.enter-hook

clean: as_root
	rm -rf /var/netsender
	rm -rf /var/log/netsender
	rm -rf /etc/systemd/system/i2c-netsender.service
	rm -rf /etc/netsender.conf
	@echo "Clean complete"
