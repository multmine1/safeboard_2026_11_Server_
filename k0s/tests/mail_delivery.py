"""Live acceptance of generated mail routes and accounts using standard clients."""

import imaplib
import json
import smtplib
import ssl
import sys
import time
from email.message import EmailMessage
from email.parser import BytesParser
from email.policy import default
from email.utils import formatdate, make_msgid


def tls_context(configuration):
    return ssl.create_default_context(cadata=configuration["ca"])


def receive_and_remove_probe(client, configuration, message, first_uid):
    for attempt in range(configuration["delivery_attempts"]):
        client.select("INBOX")
        status, matches = client.uid("SEARCH", None, "UID", f"{first_uid}:*")
        if status != "OK":
            raise RuntimeError("mail_search_failed")
        for uid in (matches[0] or b"").split():
            if int(uid) < first_uid:
                continue
            status, data = client.uid("FETCH", uid, "(BODY.PEEK[])")
            received = BytesParser(policy=default).parsebytes(
                b"".join(item[1] for item in data if isinstance(item, tuple)))
            if str(received.get("Message-ID")) != str(message["Message-ID"]):
                continue
            if status != "OK" or received.get_content().splitlines() != message.get_content().splitlines():
                raise RuntimeError("mail_content_mismatch")
            if client.uid("STORE", uid, "+FLAGS.SILENT", "(\\Deleted)")[0] != "OK":
                raise RuntimeError("probe_cleanup_flag_failed")
            if client.uid("EXPUNGE", uid)[0] != "OK":
                raise RuntimeError("probe_cleanup_expunge_failed")
            return
        if attempt + 1 < configuration["delivery_attempts"]:
            time.sleep(configuration["delivery_delay_seconds"])
    raise TimeoutError("probe_mail_not_delivered")


def build_probe_message(configuration):
    message = EmailMessage()
    message["From"] = configuration["sender_address"]
    message["To"] = configuration["recipient_address"]
    message["Subject"] = "Infrastructure mail acceptance"
    message["Date"] = formatdate(usegmt=True)
    message["Message-ID"] = make_msgid(domain=configuration["message_id_domain"])
    message.set_content("Temporary infrastructure acceptance message.\n" + str(message["Message-ID"]))
    return message


def send_probe_message(configuration, message, context, observations):
    endpoint = configuration["smtp"]
    with smtplib.SMTP_SSL(endpoint["host"], endpoint["port"], context=context,
                          timeout=configuration["timeout_seconds"]) as client:
        observations["smtp_tls_verified"] = True
        client.login(configuration["sender_address"], configuration["sender_password"])
        observations["smtp_authenticated"] = True
        if client.send_message(message):
            raise RuntimeError("recipient_rejected")
        observations["smtp_accepted"] = True


def check_round_trip(configuration, observations):
    context = tls_context(configuration)
    message = build_probe_message(configuration)
    endpoint = configuration["imap"]
    with imaplib.IMAP4_SSL(endpoint["host"], endpoint["port"], ssl_context=context,
                         timeout=configuration["timeout_seconds"]) as client:
        observations["imap_tls_verified"] = True
        client.login(configuration["recipient_address"], configuration["recipient_password"])
        observations["imap_authenticated"] = True
        client.select("INBOX")
        first_uid = int(client.response("UIDNEXT")[1][0])
        send_probe_message(configuration, message, context, observations)
        receive_and_remove_probe(client, configuration, message, first_uid)
    return {"tls_verified": True, "authenticated_delivery": True, "imap_received": True,
            "probe_message_removed": True}


def check_sender_cannot_read_mail(configuration):
    endpoint = configuration["imap"]
    client = imaplib.IMAP4_SSL(endpoint["host"], endpoint["port"], ssl_context=tls_context(configuration),
                              timeout=configuration["timeout_seconds"])
    try:
        try:
            client.login(configuration["sender_address"], configuration["sender_password"])
        except imaplib.IMAP4.error:
            return {"sender_imap_denied": True}
    finally:
        client.shutdown()
    raise RuntimeError("send_only_account_can_read_mail")


def check_relay(configuration):
    endpoint = configuration["inbound"]
    with smtplib.SMTP(endpoint["host"], endpoint["port"],
                      timeout=configuration["timeout_seconds"]) as client:
        code, _ = client.ehlo(endpoint["host"])
        if code != 250 or client.mail("")[0] != 250:
            raise RuntimeError("inbound_envelope_rejected")
        if client.rcpt(configuration["recipient_address"])[0] != 250:
            raise RuntimeError("local_inbound_recipient_rejected")
        client.rset()
        client.mail("")
        code, _ = client.rcpt(configuration["external_probe_recipient"])
        client.rset()
        if code not in configuration["relay_rejection_codes"]:
            raise RuntimeError("anonymous_external_recipient_not_rejected")
    return {"local_inbound_recipient_accepted": True, "anonymous_relay_denied": True}


if __name__ == "__main__":
    observations = {}
    try:
        configuration = json.load(sys.stdin)
        if sys.argv[1] == "relay":
            observations = check_relay(configuration)
        else:
            observations.update(check_round_trip(configuration, observations))
            observations.update(check_sender_cannot_read_mail(configuration))
        print(json.dumps(observations))
    except Exception as error:
        print(json.dumps({"error_type": type(error).__name__,
                          "smtp_code": getattr(error, "smtp_code", None),
                          "reason": str(error) if isinstance(error, (RuntimeError, TimeoutError)) else None,
                          "observations": observations}))
        raise SystemExit(1) from None
