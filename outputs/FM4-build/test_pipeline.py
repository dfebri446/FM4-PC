"""Synthetic tests: no FM4 bytes or original game assets are needed."""
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

import pipeline
from prepare import switches, BASE

class PipelineTests(unittest.TestCase):
    def dispatch(self):
        # Construct a synthetic lis/addi/lwzx/mtctr/bctr dispatch for r3.
        words = [(15 << 26) | (4 << 21) | 0x8200,
                 (14 << 26) | (4 << 21) | (4 << 16) | 64,
                 (31 << 26) | (5 << 21) | (4 << 16) | (3 << 11) | (23 << 1),
                 (31 << 26) | (5 << 21) | (9 << 16) | (467 << 1), 0x4E800420]
        image = bytearray(128)
        for i, word in enumerate(words): struct.pack_into('>I',image,i*4,word)
        struct.pack_into('>I',image,64,BASE+96)
        recipe = {'start':BASE,'end':BASE+16,'address':BASE,'register':3,'count':1}
        return image,recipe

    def test_reads_destination_from_input(self):
        image,recipe=self.dispatch()
        self.assertEqual(switches(image,[recipe])[0]['labels'],[BASE+96])
        struct.pack_into('>I',image,64,BASE+100)
        self.assertEqual(switches(image,[recipe])[0]['labels'],[BASE+100])

    def test_rejects_unsupported_or_bad_destinations(self):
        image,recipe=self.dispatch()
        with self.assertRaises(ValueError): switches(image[:65],[recipe])
        struct.pack_into('>I',image,64,3)
        with self.assertRaises(ValueError): switches(image,[recipe])
        image[0:4]=bytes(4)
        with self.assertRaises(ValueError): switches(image,[recipe])

    def test_rejects_incomplete_dispatch(self):
        image,recipe=self.dispatch()
        recipe['end']=BASE+12
        with self.assertRaises(ValueError): switches(image,[recipe])

    def test_hash_validation_and_disc2_paths(self):
        with tempfile.TemporaryDirectory(prefix='fm4 source test ') as directory:
            root=Path(directory)
            executable=root/'default.xex'
            executable.write_text('synthetic executable placeholder')
            with patch.object(pipeline,'XEX_HASHES',{'default.xex':pipeline.digest(executable)}):
                with self.assertRaises(ValueError): pipeline.validate_discs(root,None)
                (root/'Media').mkdir()
                (root/'Database.xmplr').write_text('synthetic placeholder')
                self.assertEqual(pipeline.validate_discs(root,None),(root.resolve(),None))
                with self.assertRaises(ValueError): pipeline.validate_discs(root,root/'missing disc 2')
                executable.write_text('wrong revision')
                with self.assertRaises(ValueError): pipeline.validate_discs(root,None)

    def test_new_clone_has_all_source_inputs(self):
        for name in ('sdk.patch','rexglue.cmake','guard-generated.py','dump-image.cpp','switch-recipes.json'):
            self.assertTrue((pipeline.HERE/name).is_file(),name)
        for recipe in json.loads((pipeline.HERE/'switch-recipes.json').read_text()):
            self.assertEqual(set(recipe),{'start','end','address','register','count'})
            self.assertNotIn('labels',recipe)

if __name__=='__main__': unittest.main()
