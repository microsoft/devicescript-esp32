# Certificates

The built-in certificate list is intersection of the 
[list used by Mozilla](https://hg.mozilla.org/releases/mozilla-release/raw-file/default/security/nss/lib/ckfw/builtins/certdata.txt)
with the [Microsoft Trusted Root Program](https://docs.microsoft.com/en-us/security/trusted-root/participants-list)

To re-generate the list do the following:

```bash
curl https://raw.githubusercontent.com/curl/curl/master/scripts/mk-ca-bundle.pl > mk-ca-bundle.pl
perl mk-ca-bundle.pl -f
node filter-bundle.js ca-bundle.crt ca-bundle.pem
```

The `ca-bundle.pem` file should be checked in.

