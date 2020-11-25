VERSION = 2.0.0

PPSFILESBAK=`find . -name pps-files.cpp.bak`

all:
	@if [ -z "$(PPSFILESBAK)" ]; then echo " "; echo "   Did you forget to create backups?"; echo " "; fi
	
	cp client/pps-files.cpp.bak client/pps-files.cpp
	sed -i "s|XXXX|`grep 'configdir' pps-client.conf | xargs | cut -c12- -`|g" client/pps-files.cpp
	
	cp client/makefile.bak client/makefile
	sed -i "s|XXXX|`grep 'libdir' pps-client.conf | xargs | cut -c9- -`|g" client/makefile
	
	cp installer/remove/pps-client-remove.cpp.bak installer/remove/pps-client-remove.cpp
	sed -i "s|XXXX|`grep 'configdir' pps-client.conf | xargs | cut -c12- -`|g" installer/remove/pps-client-remove.cpp

#	cp utils/interrupt-timer/makefile.bak utils/interrupt-timer/makefile
#	sed -i "s|XXXX|`grep 'execdir' pps-client.conf | xargs | cut -c10- -`|g" utils/interrupt-timer/makefile

#	cp utils/interrupt-timer/driver/Makefile.bak utils/interrupt-timer/driver/Makefile
#	sed -i "s|XXXX|`grep 'builddir' pps-client.conf | xargs | cut -c11- -`|g" utils/interrupt-timer/driver/Makefile
	
#	cp utils/pulse-generator/makefile.bak utils/pulse-generator/makefile
#	sed -i "s|XXXX|`grep 'execdir' pps-client.conf | xargs | cut -c10- -`|g" utils/pulse-generator/makefile

#	cp utils/pulse-generator/driver/Makefile.bak utils/pulse-generator/driver/Makefile
#	sed -i "s|XXXX|`grep 'builddir' pps-client.conf | xargs | cut -c11- -`|g" utils/pulse-generator/driver/Makefile
	
	cp utils/pps-timer/makefile.bak utils/pps-timer/makefile
	sed -i "s|XXXX|`grep 'execdir' pps-client.conf | xargs | cut -c10- -`|g" utils/pps-timer/makefile

	cp utils/pps-timer/driver/Makefile.bak utils/pps-timer/driver/Makefile
	sed -i "s|XXXX|`grep 'builddir' pps-client.conf | xargs | cut -c11- -`|g" utils/pps-timer/driver/Makefile

	cp utils/NormalDistribParams/makefile.bak utils/NormalDistribParams/makefile
	sed -i "s|XXXX|`grep 'execdir' pps-client.conf | xargs | cut -c10- -`|g" utils/NormalDistribParams/makefile

	cp utils/udp-time-client/makefile.bak utils/udp-time-client/makefile
	sed -i "s|XXXX|`grep 'execdir' pps-client.conf | xargs | cut -c10- -`|g" utils/udp-time-client/makefile

	mkdir pkg
	mkdir pkg/client
	mkdir pkg/client/figures
	mkdir tmp
	
	cd ./client && $(MAKE) all
	cp ./client/pps-client ./pkg/pps-client
	
	cp -r ./installer/install-head/. ./tmp
	cd ./tmp && $(MAKE) all
	cp ./tmp/pps-client-install-hd ./installer/pps-client-install-hd
	find ./tmp -type f -delete
	
	cp ./client/pps-client-stop.sh ./pkg/pps-client-stop
	
	cp -r ./installer/remove/. ./tmp
	cd ./tmp && $(MAKE) all
	cp ./tmp/pps-client-remove ./pkg/pps-client-remove
	find ./tmp -type f -delete
	
	cp -r ./installer/make-install/. ./tmp
	cd ./tmp && $(MAKE) all
	cp ./tmp/pps-client-make-install ./installer/pps-client-make-install
	find ./tmp -type f -delete
		
	cp -r ./utils/NormalDistribParams/. ./tmp
	cd ./tmp && $(MAKE) all
	cp ./tmp/normal-params ./pkg/normal-params
	find ./tmp -type f -delete
	
	cp -r ./utils/udp-time-client/. ./tmp
	cd ./tmp && $(MAKE) all
	cp ./tmp/udp-time-client ./pkg/udp-time-client
	find ./tmp -type f -delete

	cp ./README.md ./pkg/README.md
	cp ./figures/RPi_with_GPS.jpg ./pkg/RPi_with_GPS.jpg
	cp ./figures/frequency-vars.png ./pkg/frequency-vars.png
	cp ./figures/offset-distrib.png ./pkg/offset-distrib.png
	cp ./figures/StatusPrintoutAt10Min.png ./pkg/StatusPrintoutAt10Min.png
	cp ./figures/StatusPrintoutOnStart.png ./pkg/StatusPrintoutOnStart.png
	cp ./figures/InterruptTimerDistrib.png ./pkg/InterruptTimerDistrib.png
#	cp ./figures/SingleEventTimerDistrib.png ./pkg/SingleEventTimerDistrib.png
	cp ./figures/time.png ./pkg/time.png
	
	cp ./Doxyfile ./pkg/Doxyfile
	cp ./client/pps-client.md ./pkg/client/pps-client.md
#	cp ./client/figures/accuracy_verify.jpg ./pkg/client/figures/accuracy_verify.jpg
#	cp ./client/figures/interrupt-delay-comparison.png ./pkg/client/figures/interrupt-delay-comparison.png
#	cp ./client/figures/InterruptTimerDistrib.png ./pkg/client/figures/InterruptTimerDistrib.png
	cp ./client/figures/jitter-spike.png ./pkg/client/figures/jitter-spike.png
#	cp ./client/figures/pps-jitter-distrib.png ./pkg/client/figures/pps-jitter-distrib.png
	cp ./client/figures/pps-offsets-stress.png ./pkg/client/figures/pps-offsets-stress.png
	cp ./client/figures/pps-offsets-to-300.png ./pkg/client/figures/pps-offsets-to-300.png
	cp ./client/figures/pps-offsets-to-720.png ./pkg/client/figures/pps-offsets-to-720.png
	cp ./client/figures/StatusPrintoutAt10Min.png ./pkg/client/figures/StatusPrintoutAt10Min.png
	cp ./client/figures/StatusPrintoutOnStart.png ./pkg/client/figures/StatusPrintoutOnStart.png
#	cp ./client/figures/wiring.png ./pkg/client/figures/wiring.png
#	cp ./client/figures/interrupt-delay-comparison-RPi3.png ./pkg/client/figures/interrupt-delay-comparison-RPi3.png
	cp ./client/figures/pps-jitter-distrib-RPi3.png ./pkg/client/figures/pps-jitter-distrib-RPi3.png
	
	
	cp ./pps-client.conf ./pkg/pps-client.conf
	cp ./client/pps-client.service ./pkg/pps-client.service
	tar czf pkg.tar.gz ./pkg
	./installer/pps-client-make-install $(shell uname -r)
	
	rm pkg.tar.gz
	rm -rf ./tmp
	@echo "Compliled successfully"
	
clean:
	rm -rf ./pkg
	rm -rf ./tmp
	
	cd ./client && $(MAKE) clean
	cd ./utils/NormalDistribParams && $(MAKE) clean
	cd ./utils/udp-time-client && $(MAKE) clean
		
	rm ./installer/pps-client-install-hd
	rm ./installer/pps-client-make-install

install:
	./pps-client-`uname -r`
	
$(V).SILENT:
