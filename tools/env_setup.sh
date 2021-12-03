#!/bin/bash

[ -e ${PWD}/.pyenv ] || {
	echo "Setting up python virtual environment"
	python3 -m venv ${PWD}/.pyenv || exit -1
}

source ${PWD}/.pyenv/bin/activate

pip3 install -r $(dirname "$0")/requirements.txt

