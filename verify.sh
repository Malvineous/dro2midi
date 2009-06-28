#!/bin/sh

tmp_midi="/tmp/verify.mid.$$"
tmp_logf="/tmp/verify.log.$$"

trap 'rm -f "${tmp_midi}" "${tmp_logf}"' 0

for midi_file in ./testcases/*.mid
do
	for format in raw dro imf wlf
	do
		bfname="$(basename "${midi_file}" ".mid")"
		ifname="./testcases/${bfname}.${format}"

		[ -r "${ifname}" ] || continue

		printf "Verifying:\t%-48s" "${ifname}"

		wine ./dro2midi.exe "${ifname}" "${tmp_midi}" > "${tmp_logf}" 2>&1

		if [ -r "${tmp_midi}" ] && cmp -s "${tmp_midi}" "${midi_file}"
		then
			echo " [OK]"
		else
			echo " [FAILED]"
			echo
			cat "${tmp_logf}"
			echo
			echo "Converting ${ifname} into ${format} format failed."
			exit
		fi
	done
done
