<?php
// Use the app's native commands; secret arguments remain inside this PHP process.
define('OC_CONSOLE', true);
require_once 'lib/base.php';
\OC_App::loadApp('mail');

function FindAccount($service, string $user, string $email) {
    $accounts = array_values(array_filter($service->findByUserId($user),
        fn($account) => $account->getEmail() === $email));
    if (count($accounts) > 1) {
        throw new RuntimeException('Multiple accounts match the declared mailbox');
    }
    return $accounts[0] ?? null;
}

function MatchesAccount($account, array $desired, $crypto): bool {
    $fields = [
        'name' => 'getName', 'email' => 'getEmail',
        'imap-host' => 'getInboundHost', 'imap-port' => 'getInboundPort',
        'imap-ssl-mode' => 'getInboundSslMode', 'imap-user' => 'getInboundUser',
        'smtp-host' => 'getOutboundHost', 'smtp-port' => 'getOutboundPort',
        'smtp-ssl-mode' => 'getOutboundSslMode', 'smtp-user' => 'getOutboundUser',
    ];
    $native = $account->getMailAccount();
    foreach ($fields as $field => $getter) {
        if ((string) $native->$getter() !== (string) $desired[$field]) {
            return false;
        }
    }
    return $native->getAuthMethod() === 'password'
        && $native->getInboundPassword() !== null && $native->getOutboundPassword() !== null
        && $crypto->decrypt($native->getInboundPassword()) === $desired['imap-password']
        && $crypto->decrypt($native->getOutboundPassword()) === $desired['smtp-password'];
}

$configuration = json_decode(stream_get_contents(STDIN), true, 512, JSON_THROW_ON_ERROR);
$container = (new \OCA\Mail\AppInfo\Application())->getContainer();
$service = $container->get(\OCA\Mail\Service\AccountService::class);
$desired = $configuration['account'] + [
    'imap-password' => $configuration['password'],
    'smtp-password' => $configuration['password'],
];
$account = FindAccount($service, $configuration['user'], $desired['email']);
$changed = $account === null || !MatchesAccount($account, $desired, \OCP\Server::get(\OCP\Security\ICrypto::class));
if ($changed) {
    if ($account === null) {
        $command = $container->get(\OCA\Mail\Command\CreateImapAccount::class);
        $arguments = ['user-id' => $configuration['user']] + $desired;
    } else {
        $command = $container->get(\OCA\Mail\Command\UpdateImapAccount::class);
        $arguments = ['account-id' => $account->getId(), '--auth-method' => 'password'];
        foreach ($desired as $name => $value) {
            $arguments['--' . $name] = $value;
        }
    }
    $input = new \Symfony\Component\Console\Input\ArrayInput($arguments);
    $input->setInteractive(false);
    if ($command->run($input, new \Symfony\Component\Console\Output\BufferedOutput()) !== 0) {
        throw new RuntimeException('Native mail account command failed');
    }
    $account = FindAccount($service, $configuration['user'], $desired['email']);
}
echo json_encode(['account_id' => $account->getId(), 'changed' => $changed], JSON_THROW_ON_ERROR);
