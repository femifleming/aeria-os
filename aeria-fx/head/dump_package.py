"""Build AERIA DUMP (diagnostic: plays the core's peripheral registers out as audio) with package.py's tools,
leaving TAP and HEAD alone.  Run: aeria-fx/head/run.sh python3 -B /h/dump_package.py"""
import importlib.util, sys
spec = importlib.util.spec_from_file_location('aeria_package', '/h/package_lib.py')
pk = importlib.util.module_from_spec(spec)
sys.modules['aeria_package'] = pk
spec.loader.exec_module(pk)
pk.MODULES = {
    'aeria-dump': dict(asm='/h/dump.asm', key='AERIA_DUMP', fx=0x1e, abbr='DUMP', full='AERIA DUMP', name='AERIA DUMP',
                       params=[], slots='both', cycles=160, state=2, out='aeria-dump.mwrm', keybus=False),
}
pk.main()
