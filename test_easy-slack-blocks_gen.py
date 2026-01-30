import easy_slack_blocks
print(f'Successfully imported easy_slack_blocks')
try:
    import easy_slack_blocks.utils
    print('Successfully imported easy_slack_blocks.utils')
except ImportError: pass
