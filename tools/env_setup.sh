#!/bin/bash

which python3 || alias python3=python

load_venv()
{
	if [ -e ${PWD}/.venv/Scripts ]; then
		source ${PWD}/.venv/Scripts/activate
	elif [ -e ${PWD}/.venv/bin ]; then
		source ${PWD}/.venv/bin/activate
	else
		echo "Error. Could not find python virtual environment activation script."
		exit -1
	fi
}

[ -e ${PWD}/.venv ] || {
	echo "Setting up python virtual environment"
	python3 -m virtualenv ${PWD}/.venv || exit -1
	load_venv || exit -1
	pip3 install -r $(dirname "$0")/requirements.txt || rm -rf ${PWD}/.venv
}

load_venv || exit -1
