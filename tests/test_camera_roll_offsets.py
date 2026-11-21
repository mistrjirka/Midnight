"""Camera roll and small pointing-offset regression on actual sky solver.

The same sky field is observed at different sensor rolls, translations of
the optical axis, and focal lengths, with ordinary 600D sensor dimensions.
"""
from __future__ import annotations

import ctypes as C
import random
import pytest

from native import DATA, MAX_STARS, H, W, SkySolution, Star, port_instance, separation
from tools.fixtures import render_star_field

pytestmark=pytest.mark.skipif(not (DATA / "SKY.DIR").exists(),
                             reason="requires an installed sky index")

@pytest.fixture(scope="module")
def lib():
    port=port_instance("roll_offset")
    assert port.harness_open_sky(str(DATA).encode(),b"SKY")==0
    port.harness_detect.argtypes=[
        C.c_char_p,C.c_int,C.c_int,C.c_int,C.POINTER(Star),C.c_int]
    return port

@pytest.mark.parametrize("field_ra,field_dec,focal",[
    (83.82,-5.39,85), (10.68,41.27,135), (279.23,38.78,135)
])
def test_random_roll_and_small_pointing_offsets(lib,field_ra,field_dec,focal):
    rng=random.Random(round(field_ra*1000))
    for i in range(12):
        roll=-179+358*i/11
        dra=rng.uniform(-0.18,0.18)
        ddec=rng.uniform(-0.18,0.18)
        ra=(field_ra+dra)%360
        dec=field_dec+ddec
        image=render_star_field(DATA/"STARS.BIN",ra,dec,roll,focal,
                                sensor_mm=22.3,maglim=7.4,seed=2233+i)
        buffer=C.create_string_buffer(image.tobytes(),W*H)
        detected=(Star*MAX_STARS)()
        count=lib.harness_detect(buffer,W,H,W,detected,MAX_STARS)
        assert count>=8,(field_ra,field_dec,roll,dra,ddec,count)
        solved=SkySolution()
        result=lib.harness_solve(detected,count,W,H,None,C.byref(solved))
        assert result==1,(field_ra,field_dec,roll,dra,ddec,count,
                           solved.checked,solved.quads)
        error=separation(solved.ra/1e4,solved.dec/1e4,ra,dec)
        assert error<0.1,(field_ra,field_dec,roll,dra,ddec,error)
        assert solved.matched>=8
