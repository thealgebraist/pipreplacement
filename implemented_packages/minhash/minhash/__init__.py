class MinimalHasher:
    def minimal_hash(self, kwargs):
        if not kwargs or 'num_elements' not in kwargs:
            raise ValueError("Invalid arguments")

        string = str(kwargs['string'])
        num_elements = kwargs['num_elements']

        hash_value = sum(hash(ord(c)) for c in string) % (2 ** num_elements)
        min_hash = [hash_value]

        for _ in range(num_elements - 1):
            hash_value += len(string)
            hash_value %= (2 ** num_elements)
            min_hash.append(hash_value)

        return min_hash

class MinimalHash:
    def __init__(self, string, num_elements=None):
        self.string = string
        self.minimal_hasher = MinimalHasher()
        if num_elements is not None:
            self.num_elements = num_elements
        else:
            self.num_elements = 10

    @property
    def minimal_hash(self):
        return self.minimal_hasher.minimal_hash({'string': self.string, 'num_elements': self.num_elements})