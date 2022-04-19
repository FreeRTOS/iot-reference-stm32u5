class AWSHelper:
    session = None
    session_valid = False
    iot_client = None
    thing = None
    userId = None
    account = None
    arn = None

    def __init__(self, args):
        profile = "default"
        if "aws-profile" in args:
            profile = args.aws_profile

        region = None
        if "aws-region" in args:
            region = args.aws_region

        access_key_id = None
        if "aws-access-key-id" in args:
            access_key_id = args.aws_access_key_id

        secret_access_key = None
        if "aws-access-key-secret" in args:
            secret_access_key = args.aws_access_key_secret

        if "policies" in args:
            policies = args.policies

        self.session = boto3.session.Session(
            profile_name=profile,
            region_name=region,
            aws_access_key_id=access_key_id,
            aws_secret_access_key=secret_access_key,
        )
        self.check_credentials()

    def check_credentials(self):
        if not self.session_valid and self.session:
            sts = self.session.client("sts")
            caller_id = sts.get_caller_identity()
            if caller_id:
                self.session_valid = True
                self.userId = caller_id["UserId"]
                self.account = caller_id["Account"]
                self.arn = caller_id["Arn"]
        return self.session_valid

    def get_session(self):
        if self.check_credentials():
            return self.session
        else:
            return None

    def get_client(self, client_type):
        client = None
        if self.get_session():
            client = self.session.client(client_type)
        return client

    def get_endpoint(self):
        endpoint_address = ""
        if not self.iot_client:
            self.iot_client = self.get_client("iot")

        if self.iot_client:
            response = self.iot_client.describe_endpoint(endpointType="iot:Data-ATS")

            if "endpointAddress" in response:
                endpoint_address = response["endpointAddress"]

        return endpoint_address

    def load_local_policies(self):
        local_policies = dict()
        tools_dir = os.path.dirname(os.path.realpath(__file__))
        local_policies_dir = os.path.join(tools_dir, "IotCorePolicies")
        local_policy_files = [ fname for fname in os.listdir(local_policies_dir) if ".json" in fname ]
        for fname in local_policy_files:
            with file as open(fname, 'rb'):
                policy_basename = os.path.basename(fname).replace(".json","")
                policy_doc = file.read()
                # Correct line endings
                policy_doc = policy_doc.replace(b'\r\n',b'\n')
                policy_hash = hashlib.sha1(policy_doc).hexdigest()[0:10]
                policy_name = '_'.join(policy_basename,policy_hash)
                policy_doc = json.loads(policy_doc)
                local_policies[policy_name] = policy_doc
        return local_policies

    def get_remote_policies_set(self):
        remote_policies = list()
        policies = self.iot_client.list_policies()
        if 'policies' in policies:
            for policy in policies:
                remote_policies.append(policy.policyName)
        return set(remote_policies)


    def snyc_policies(self):
        remote = self.get_remote_policies_set()
        local = self.load_local_policies()

        for (name,policy_doc) in policies_dict.items():
            if name not in remote:
                create_policy_resp = self.iot_client.create_policy(
                    policyName=policy_name,
                    policyDocument=json.dumps(policy_doc))





    def import_policies(self):
        if not self.iot_client:
            self.iot_client = self.get_client("iot")



        policies = self.iot_client.list_policies()

        policyFound = False
        for policy in policies["policies"]:
            logger.debug("Found Policy: {}".format(policy["policyName"]))
            if policy["policyName"] == "ProvisioningScriptPolicy":
                policyFound = True

        if policyFound:
            logger.debug('Found existing "ProvisioningScriptPolicy" IoT core policy.')
        else:
            logger.info(
                'Existing policy "ProvisioningScriptPolicy" was not found. Creating it...'
            )


        print(json.dumps(policyDocument))

        if not policyFound:
            policy = self.iot_client.create_policy(
                policyName="ProvisioningScriptPolicy",
                policyDocument=json.dumps(policyDocument),
            )


    def register_thing_csr(self, thing_name, csr):
        """Request a certificate using the given CSR and register the given device with AWS IoT Core"""
        if not self.iot_client:
            self.iot_client = self.get_client("iot")

        assert self.iot_client

        self.thing = {}

        cli = self.iot_client

        cert_response = cli.create_certificate_from_csr(
            certificateSigningRequest=csr, setAsActive=True
        )
        logging.debug("CreateCertificateFromCsr response: {}".format(cert_response))
        self.thing.update(cert_response)

        create_thing_resp = cli.create_thing(thingName=thing_name)
        logging.debug("CreateThing response: {}".format(create_thing_resp))
        self.thing.update(create_thing_resp)

        if not (
            "certificateArn" in self.thing
            and "thingName" in self.thing
            and "certificatePem" in self.thing
        ):
            logging.error("Error: Certificate creation failed.")
        else:
            print(
                "Attaching thing: {} to principal: {}".format(
                    self.thing["thingName"], self.thing["certificateArn"]
                )
            )
            cli.attach_thing_principal(
                thingName=self.thing["thingName"],
                principal=self.thing["certificateArn"],
            )

        # Check for / create Policy
        self.create_policy()

        # Attach the policy to the principal.
        print(
            'Attaching the "ProvisioningScriptPolicy" policy to the device certificate.'
        )
        self.iot_client.attach_policy(
            policyName="ProvisioningScriptPolicy", target=self.thing["certificateArn"]
        )

        self.thing["certificatePem"] = bytes(
            self.thing["certificatePem"].replace("\\n", "\n"), "ascii"
        )

        return self.thing.copy()


    def register_thing_cert(self, thing_name, cert):
        """Register a device with IoT core with a given certificate"""
        if not self.iot_client:
            self.iot_client = self.get_client("iot")

        assert self.iot_client

        self.thing = {}

        cli = self.iot_client

        cert_response = cli.register_certificate_without_ca(
            certificatePem=cert, status="ACTIVE"
        )
        logging.debug("RegisterCertificateWithoutCA response: {}".format(cert_response))
        self.thing.update(cert_response)

        create_thing_resp = cli.create_thing(thingName=thing_name)
        logging.debug("CreateThing response: {}".format(create_thing_resp))
        self.thing.update(create_thing_resp)

        if not ("certificateArn" in self.thing and "thingName" in self.thing):
            logging.error("Error: Certificate creation failed.")
        else:
            print(
                "Attaching thing: {} to principal: {}".format(
                    self.thing["thingName"], self.thing["certificateArn"]
                )
            )
            cli.attach_thing_principal(
                thingName=self.thing["thingName"],
                principal=self.thing["certificateArn"],
            )

        # Check for / create Policy
        self.create_policy()

        # Attach the policy to the principal.
        print(
            'Attaching the "ProvisioningScriptPolicy" policy to the device certificate.'
        )
        self.iot_client.attach_policy(
            policyName="ProvisioningScriptPolicy", target=self.thing["certificateArn"]
        )

        return self.thing.copy()

