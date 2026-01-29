import unittest
from minhash import minimal_hash, MinimalHash
class TestMinimalHash(unittest.TestCase):

    def test_single_string(self):
        self.assertEqual(minimal_hash('test'), [145, 111, 224])

    def test_multiple_strings(self):
        self.assertEqual(minimal_hash(*['abc', 'def']), [710309077, 589689394, 1014899578])

    def test_invalid_num_elements(self):
        with self.assertRaises(ValueError):
            minimal_hash('test', 1)

if __name__ == '__main__':
    unittest.main()