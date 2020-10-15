import json
import tempfile
import subprocess
import logging
import signal
import requests
import datetime

LOGGER = logging.getLogger('villas.node')


class Node(object):

    api_version = 'v2'

    def __init__(self, api_url=None,
                 log_filename=None,
                 config_filename=None, config={},
                 executable='villas-node', **kwargs):

        self.api_url = api_url
        self.log_filename = log_filename
        self.config = config
        self.config_filename = config_filename
        self.executable = executable

    def start(self):
        self.config_file = tempfile.NamedTemporaryFile(mode='w+',
                                                       suffix='.json')

        if self.config_filename:
            with open(self.config_filename) as f:
                self.config_file.write(f.read())
        else:
            json.dump(self.config, self.config_file)

        self.config_file.flush()

        if self.log_filename is None:
            now = datetime.datetime.now()
            self.log_filename = now.strftime(
                'villas-node_%Y-%m-%d_%H-%M-%S.log')

        self.log = open(self.log_filename, 'w+')

        LOGGER.info("Starting VILLASnode instance with config: %s",
                    self.config_file.name)

        self.child = subprocess.Popen([self.executable, self.config_file.name],
                                      stdout=self.log, stderr=self.log)

    def pause(self):
        LOGGER.info("Pausing VILLASnode instance")
        self.child.send_signal(signal.SIGSTOP)

    def resume(self):
        LOGGER.info("Resuming VILLASnode instance")
        self.child.send_signal(signal.SIGCONT)

    def stop(self):
        LOGGER.info("Stopping VILLASnode instance")
        self.child.send_signal(signal.SIGTERM)
        self.child.wait()

        self.log.close()

    def restart(self):
        LOGGER.info("Restarting VILLASnode instance")
        self.request('restart')

    @property
    def active_config(self):
        resp = self.request('config')

        return resp

    @property
    def nodes(self):
        return self.request('nodes')

    @property
    def paths(self):
        return self.request('paths')

    def request(self, action, req=None):
        args = {
            'timeout': 1
        }

        if req is not None:
            args['json'] = req

        r = requests.get(
            f'{self.api_url}/api/{self.api_version}/{action}', **args)

        r.raise_for_status()

        return r.json()

    def get_local_version(self):
        ver = subprocess.check_output([self.executable, '-V'])

        return ver.decode('ascii').rstrip()

    def get_version(self):
        resp = self.request('capabilities')

        return resp['build']

    def is_running(self):
        if self.child is None:
            return False
        else:
            return self.child.poll() is None
