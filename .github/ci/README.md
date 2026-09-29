`debug.keystore` is a throwaway key used only to sign the debug APKs built
by `.github/workflows/android-apk.yml`, so that each new build can be
installed over the previous one. It uses Android's standard debug key
settings (store/key password `android`, alias `androiddebugkey`). Do not use
it to sign anything that is distributed.
